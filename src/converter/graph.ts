import protobuf from 'protobufjs';
// ONNX v1.18.0 schema, SPDX-License-Identifier: Apache-2.0.
import schema from './schema.json';
import type { ModelConfig, Weight } from './release.js';

// Matches MTranCore's independently verified Marian rank-4 graph and ORT quantizers.
const modelType = protobuf.Root.fromJSON(schema).lookupType('onnx.ModelProto');
interface Attribute { name: string; type: number; i?: number; ints?: number[]; f?: number; g?: Graph }
interface Tensor { name: string; dims: number[]; dataType: number; rawData?: Uint8Array; int64Data?: number[] }
interface Node { name: string; opType: string; input: string[]; output: string[]; attribute: Attribute[]; domain?: string }
interface Value { name: string; type: object }
interface Graph { name: string; node: Node[]; initializer: Tensor[]; input: Value[]; output: Value[] }
export type Mode = 'fp32' | 'int4-k9' | 'qdecoder' | 'qall';
const graph = (name: string): Graph => ({ name, node: [], initializer: [], input: [], output: [] });
const ia = (name: string, i: number): Attribute => ({ name, type: 2, i });
const isa = (name: string, ...ints: number[]): Attribute => ({ name, type: 7, ints });
const fa = (name: string, f: number): Attribute => ({ name, type: 1, f: Math.fround(f) });
const value = (name: string, elemType: number, ...dims: number[]): Value => ({ name, type: { tensorType: { elemType, shape: { dim: dims.map((d, i) => d < 0 ? { dimParam: `${name}_d${i}` } : { dimValue: d }) } } } });
const ft = (name: string, dims: number[], values: Float32Array | number[]): Tensor => {
  const rawData = Buffer.alloc(values.length * 4);
  for (let i = 0; i < values.length; i++) rawData.writeFloatLE(values[i], i * 4);
  return { name, dims, dataType: 1, rawData };
};
export function roundEven(x: number) {
  const a = Math.floor(x), r = x - a;
  return r < .5 ? a : r > .5 ? a + 1 : a % 2 === 0 ? a : a + 1;
}
export function quantize4(w: Float32Array, k: number, n: number) {
  const blocks = Math.ceil(k / 64), packed = new Uint8Array(n * blocks * 32), scales = new Float32Array(n * blocks);
  for (let col = 0; col < n; col++) for (let block = 0; block < blocks; block++) {
    let lo = 0, hi = 0;
    for (let j = 0; j < 64 && block * 64 + j < k; j++) { const v = w[(block * 64 + j) * n + col]; lo = Math.min(lo, v); hi = Math.max(hi, v); }
    const scale = Math.fround(-(Math.abs(hi) > Math.abs(lo) ? hi : lo) / 8);
    scales[col * blocks + block] = scale;
    const inverse = scale === 0 ? 0 : Math.fround(1 / scale);
    for (let j = 0; j < 64; j++) {
      const v = block * 64 + j < k ? w[(block * 64 + j) * n + col] : 0;
      const q = Math.max(0, Math.min(15, Math.round(Math.fround(Math.fround(v * inverse) + 8))));
      packed[(col * blocks + block) * 32 + (j >> 1)] |= q << ((j % 2) * 4);
    }
  }
  return { packed, scales };
}
class Builder {
  keys: string[] = []; vals: string[] = [];
  readonly dim: number; readonly headsCount: number; readonly depth: number; readonly emb: string;
  constructor(public g: Graph, public root: Graph, public serial: { n: number }, public c: ModelConfig, public weights: Map<string, Weight>, public mode: Mode) {
    this.dim = c['dim-emb']; this.headsCount = c['transformer-heads']; this.depth = c['dec-depth']; this.emb = c['tied-embeddings-all'] ? 'Wemb' : 'decoder_Wemb';
  }
  name(s: string) { return `${s}_${++this.serial.n}`; }
  node(opType: string, input: string[], ...attribute: Attribute[]) { const name = this.name(opType); this.g.node.push({ name, opType, input, output: [name], attribute }); return name; }
  op(op: string, ...input: string[]) { return this.node(op, input); }
  icon(dims: number[], ...int64Data: number[]) { const name = this.name('ints'); this.root.initializer.push({ name, dims, dataType: 7, int64Data }); return name; }
  fc(dims: number[], ...v: number[]) { const name = this.name('floats'); this.root.initializer.push(ft(name, dims, v)); return name; }
  reshape(x: string, ...dims: number[]) { return this.op('Reshape', x, this.icon([dims.length], ...dims)); }
  transpose(x: string, ...perm: number[]) { return this.node('Transpose', [x], isa('perm', ...perm)); }
  unsqueeze(x: string, ...axes: number[]) { return this.node('Unsqueeze', [x], isa('axes', ...axes)); }
  squeeze(x: string, ...axes: number[]) { return this.node('Squeeze', [x], isa('axes', ...axes)); }
  linear(x: string, w: string, bias = '') { const y = this.op('MatMul', x, w); return bias ? this.op('Add', y, bias) : y; }
  norm(x: string, p: string) {
    x = this.reshape(x, -1, 1, this.dim);
    x = this.node('InstanceNormalization', [x, this.fc([1], 1), this.fc([1], 0)], fa('epsilon', 1e-6));
    x = this.op('Add', this.op('Mul', x, p + '_scale'), p + '_bias');
    return this.reshape(x, 1, 1, -1, this.dim);
  }
  heads(x: string) { return this.transpose(this.reshape(x, 1, -1, this.headsCount, this.dim / this.headsCount), 0, 2, 1, 3); }
  attention(x: string, p: string, k: string, v: string) {
    const q = this.heads(this.linear(x, p + '_Wq', p + '_bq'));
    let scores = this.op('MatMul', q, k);
    scores = this.op('Mul', scores, this.fc([1], 1 / Math.sqrt(this.dim / this.headsCount)));
    scores = this.node('Softmax', [scores], ia('axis', 3));
    let y = this.reshape(this.transpose(this.op('MatMul', scores, v), 0, 2, 1, 3), 1, 1, -1, this.dim);
    y = this.linear(y, p + '_Wo', p + '_bo');
    return this.norm(this.op('Add', y, x), p + '_Wo_ln');
  }
  ffn(x: string, p: string) {
    let y = x;
    for (let i = 1; i <= this.c['transformer-ffn-depth']; i++) { y = this.linear(y, `${p}_W${i}`, `${p}_b${i}`); if (i < this.c['transformer-ffn-depth']) y = this.op('Relu', y); }
    return this.norm(this.op('Add', y, x), p + '_ffn_ln');
  }
  position(x: string, pos: string) {
    x = this.op('Mul', x, this.fc([1], Math.sqrt(this.dim)));
    return this.op('Add', x, this.op('Sin', this.op('Add', this.op('Mul', pos, 'position_frequency'), 'position_offset')));
  }
  decode(x: string, pos: string, cells: string[]): [string, string[]] {
    x = this.position(this.reshape(x, 1, 1, -1, this.dim), pos);
    const out: string[] = [];
    for (let i = 1; i <= this.depth; i++) {
      const p = `decoder_l${i}_rnn`, f = this.op('Sigmoid', this.linear(x, p + '_Wf', p + '_bf'));
      const cell = this.op('Add', this.op('Mul', f, cells[i - 1]), this.op('Mul', this.op('Sub', this.fc([1], 1), f), this.linear(x, p + '_W')));
      out.push(cell);
      x = this.norm(this.op('Add', this.op('Relu', cell), x), p + '_ffn_ln');
      x = this.attention(x, `decoder_l${i}_context`, this.keys[i - 1], this.vals[i - 1]);
      x = this.ffn(x, `decoder_l${i}_ffn`);
    }
    return [x, out];
  }
  project(x: string) {
    x = this.reshape(x, 1, this.dim);
    if (this.mode === 'fp32') return this.squeeze(this.node('ArgMax', [this.linear(x, 'projection_T', 'decoder_ff_logit_out_b')], ia('axis', 1), ia('keepdims', 0)), 0);
    let coarse = this.node('MatMulNBits', [x, 'projection_Q4', 'projection_scales'], ia('K', this.dim), ia('N', this.weights.get(this.emb)!.dims[0]), ia('bits', 4), ia('block_size', 64), ia('accuracy_level', 4));
    this.g.node.at(-1)!.domain = 'com.microsoft';
    coarse = this.op('Add', coarse, 'decoder_ff_logit_out_b');
    let ids = this.name('candidate_ids'); const scores = this.name('candidate_scores');
    this.g.node.push({ name: this.name('TopK'), opType: 'TopK', input: [coarse, this.icon([1], 9)], output: [scores, ids], attribute: [ia('axis', 1), ia('largest', 1), ia('sorted', 1)] });
    ids = this.squeeze(ids, 0);
    const w = this.node('Gather', [this.emb, ids], ia('axis', 0));
    const bias = this.node('Gather', ['decoder_ff_logit_out_b', ids], ia('axis', 1));
    const logits = this.op('Add', this.op('MatMul', x, this.transpose(w, 1, 0)), bias);
    const j = this.squeeze(this.node('ArgMax', [logits], ia('axis', 1), ia('keepdims', 0)), 0);
    return this.node('Gather', [ids, j], ia('axis', 0));
  }
}
function prune(g: Graph): Set<string> {
  const used = new Set(g.node.flatMap(n => n.input));
  for (const n of g.node) for (const a of n.attribute) if (a.g) for (const s of prune(a.g)) used.add(s);
  g.initializer = g.initializer.filter(t => used.has(t.name)); return used;
}
function check(g: Graph, outer = new Set<string>()) {
  const known = new Set([...outer, ...g.initializer.map(t => t.name), ...g.input.map(v => v.name)]);
  for (const n of g.node) {
    for (const s of n.input) if (s && !known.has(s)) throw new Error(`${n.name}: missing ${s}`);
    for (const a of n.attribute) if (a.g) check(a.g, known);
    for (const s of n.output) known.add(s);
  }
  for (const v of g.output) if (!known.has(v.name)) throw new Error(`Missing output ${v.name}`);
}
function dynamicInt8(root: Graph, mode: Mode) {
  const initializers = new Map(root.initializer.map(t => [t.name, t])), converted = new Set<string>();
  const rewrite = (g: Graph) => {
    const nodes: Node[] = [];
    for (const node of g.node) {
      for (const a of node.attribute) if (a.g) rewrite(a.g);
      const name = node.input[1], w = initializers.get(name);
      if (node.opType !== 'MatMul' || !w || w.dataType !== 1 || (mode === 'qdecoder' && !name.startsWith('decoder_'))) { nodes.push(node); continue; }
      const qname = name + '_quantized', sname = name + '_scale', zname = name + '_zero_point';
      if (!converted.has(name)) {
        const raw = Buffer.from(w.rawData!.buffer, w.rawData!.byteOffset, w.rawData!.byteLength);
        let amax = 0;
        for (let i = 0; i < raw.length; i += 4) amax = Math.max(amax, Math.abs(raw.readFloatLE(i)));
        let scale = Math.fround(Math.fround(amax * 2) / 254);
        if (scale < 2 ** -126) scale = 1;
        const q = new Uint8Array(raw.length / 4);
        for (let i = 0; i < q.length; i++) q[i] = Math.max(-128, Math.min(127, roundEven(Math.fround(raw.readFloatLE(i * 4) / scale))));
        root.initializer.push({ name: qname, dims: w.dims, dataType: 3, rawData: q }, ft(sname, [], [scale]), { name: zname, dims: [], dataType: 3, rawData: new Uint8Array([0]) });
        converted.add(name);
      }
      const s = node.name + '_int8';
      const emit = (opType: string, input: string[], output: string[], ...attribute: Attribute[]) => nodes.push({ name: output[0] + '_node', opType, input, output, attribute });
      emit('DynamicQuantizeLinear', [node.input[0]], [s + '_a', s + '_s', s + '_z']);
      emit('MatMulInteger', [s + '_a', qname, s + '_z', zname], [s + '_acc']);
      emit('Cast', [s + '_acc'], [s + '_float'], ia('to', 1));
      emit('Mul', [s + '_s', sname], [s + '_scale']);
      emit('Mul', [s + '_float', s + '_scale'], node.output);
    }
    g.node = nodes;
  };
  rewrite(root); prune(root);
}
export function buildModel(weights: Map<string, Weight>, c: ModelConfig, mode: Mode = 'qall'): Uint8Array {
  const g = graph('mozilla_release_greedy'), b = new Builder(g, g, { n: 0 }, c, weights, mode);
  const emb = weights.get(b.emb);
  if (!emb || emb.dims.length !== 2 || emb.dims[1] !== b.dim || emb.dims[0] < 9) throw new Error('Invalid target embedding');
  for (const name of [...weights.keys()].sort()) {
    const w = weights.get(name); if (!w) continue;
    let values = w.values;
    if (name === 'decoder_ff_logit_out_b') { values = values.slice(); if (values.length < 2) throw new Error('Invalid output bias'); values[1] = -Infinity; }
    g.initializer.push(ft(name, w.dims, values));
  }
  const frequency = new Float32Array(b.dim), offsets = new Float32Array(b.dim);
  for (let i = 0; i < b.dim; i++) { frequency[i] = Math.pow(1e-4, (i % (b.dim / 2)) / (b.dim / 2 - 1)); offsets[i] = Math.floor(i / (b.dim / 2)) * Math.PI / 2; }
  g.initializer.push(ft('position_frequency', [b.dim], frequency), ft('position_offset', [b.dim], offsets));
  const n = emb.dims[0], pt = new Float32Array(emb.values.length);
  for (let i = 0; i < n; i++) for (let j = 0; j < b.dim; j++) pt[j * n + i] = emb.values[i * b.dim + j];
  if (mode === 'fp32') g.initializer.push(ft('projection_T', [b.dim, n], pt));
  else { const { packed, scales } = quantize4(pt, b.dim, n); g.initializer.push({ name: 'projection_Q4', dims: [n, Math.ceil(b.dim / 64), 32], dataType: 2, rawData: packed }, ft('projection_scales', [n, Math.ceil(b.dim / 64)], scales)); }
  g.input = [value('X', 7, -1), value('eos_id', 7, 1)]; g.output = [value('Y', 7, -1)];
  const length = b.squeeze(b.op('Shape', 'X'), 0);
  let pos = b.node('Cast', [b.op('Range', b.icon([], 0), length, b.icon([], 1))], ia('to', 1)); pos = b.unsqueeze(pos, 1);
  let x = b.node('Gather', [c['tied-embeddings-all'] ? b.emb : 'encoder_Wemb', 'X'], ia('axis', 0));
  x = b.position(b.reshape(x, 1, 1, -1, b.dim), pos);
  for (let i = 1; i <= c['enc-depth']; i++) { const p = `encoder_l${i}_self`, k = b.transpose(b.heads(b.linear(x, p + '_Wk', p + '_bk')), 0, 1, 3, 2), v = b.heads(b.linear(x, p + '_Wv', p + '_bv')); x = b.ffn(b.attention(x, p, k, v), `encoder_l${i}_ffn`); }
  b.op('Identity', x); g.node.at(-1)!.output = ['encoder_hidden'];
  for (let i = 1; i <= b.depth; i++) { const p = `decoder_l${i}_context`; b.keys.push(b.transpose(b.heads(b.linear(x, p + '_Wk', p + '_bk')), 0, 1, 3, 2)); b.vals.push(b.heads(b.linear(x, p + '_Wv', p + '_bv'))); }
  const zero = b.fc([1, 1, 1, b.dim], ...new Float32Array(b.dim));
  const [hidden, cells] = b.decode(zero, b.fc([], 0), Array(b.depth).fill(zero)), first = b.project(hidden);
  const eos = b.squeeze('eos_id', 0), condition = b.op('Not', b.op('Equal', first, eos));
  const body = graph('decode_loop'), bb = new Builder(body, g, b.serial, c, weights, mode); bb.keys = b.keys; bb.vals = b.vals;
  body.input = [value('iteration', 7), value('condition', 9), value('previous_token', 7)];
  const states = Array.from({ length: b.depth }, (_, i) => `cell_${i}`);
  for (const s of states) body.input.push(value(s, 1, 1, 1, 1, b.dim));
  x = bb.node('Gather', [b.emb, bb.unsqueeze('previous_token', 0)], ia('axis', 0));
  const position = bb.node('Cast', [bb.op('Add', 'iteration', bb.icon([], 1))], ia('to', 1));
  const [h, cs] = bb.decode(bb.reshape(x, 1, 1, 1, b.dim), position, states), token = bb.project(h);
  const cond = bb.op('Not', bb.op('Equal', token, eos)), scan = bb.op('Identity', token);
  body.output = [value(cond, 9), value(token, 7), ...cs.map(s => value(s, 1, 1, 1, 1, b.dim)), value(scan, 7)];
  g.node.push({ name: 'decode_loop', opType: 'Loop', input: [b.op('Mul', length, b.icon([], 3)), condition, first, ...cells], output: ['last_token', ...states.map((_, i) => `last_cell_${i}`), 'generated_tokens'], attribute: [{ name: 'body', type: 5, g: body }] });
  b.node('Concat', [b.unsqueeze(first, 0), 'generated_tokens'], ia('axis', 0)); g.node.at(-1)!.output = ['Y'];
  prune(g);
  if (mode === 'qall' || mode === 'qdecoder') dynamicInt8(g, mode);
  check(g);
  const model = { irVersion: 10, producerName: 'mtranserver-ts', graph: g, opsetImport: [{ domain: '', version: 11 }, ...(mode === 'fp32' ? [] : [{ domain: 'com.microsoft', version: 1 }])] };
  const error = modelType.verify(model); if (error) throw new Error(error);
  return modelType.encode(modelType.create(model)).finish();
}
