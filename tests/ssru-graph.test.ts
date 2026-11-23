import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { test } from 'bun:test';
import protobuf from 'protobufjs';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { buildModel, type Mode } from '../src/converter/graph';
import { readRelease, type ModelConfig, type Weight } from '../src/converter/release';
import schema from '../src/converter/schema.json';

const modelType = protobuf.Root.fromJSON(schema).lookupType('onnx.ModelProto');
const decode = (bytes: Uint8Array) => modelType.toObject(modelType.decode(bytes), { longs: Number, bytes: Buffer }) as any;
const floatBytes = (values: Float32Array) => {
  const bytes = Buffer.alloc(values.length * 4);
  values.forEach((v, i) => bytes.writeFloatLE(v, i * 4));
  return bytes;
};
function fixture(dim = 4, heads = 2, depth = 2, ffnDepth = 2, tied = true) {
  const config: ModelConfig = { 'dim-emb': dim, 'transformer-heads': heads, 'enc-depth': 1, 'dec-depth': depth, 'transformer-ffn-depth': ffnDepth, 'tied-embeddings-all': tied };
  const weights = new Map<string, Weight>();
  const add = (name: string, ...dims: number[]) => weights.set(name, { dims, values: Float32Array.from({ length: dims.reduce((a, b) => a * b, 1) }, (_, i) => ((i * 7 + name.length) % 19 - 9) / 13) });
  const norm = (p: string) => { add(p + '_scale', 1, dim); add(p + '_bias', 1, dim); };
  const attention = (p: string) => {
    for (const letter of ['q', 'k', 'v', 'o']) { add(p + '_W' + letter, dim, dim); add(p + '_b' + letter, 1, dim); }
    norm(p + '_Wo_ln');
  };
  const ffn = (p: string) => {
    for (let j = 1; j <= ffnDepth; j++) { const width = j === ffnDepth ? dim : dim * 2; add(`${p}_W${j}`, j === 1 ? dim : dim * 2, width); add(`${p}_b${j}`, 1, width); }
    norm(p + '_ffn_ln');
  };
  add(tied ? 'Wemb' : 'decoder_Wemb', 12, dim);
  if (!tied) add('encoder_Wemb', 13, dim);
  add('decoder_ff_logit_out_b', 1, 12);
  attention('encoder_l1_self'); ffn('encoder_l1_ffn');
  for (let i = 1; i <= depth; i++) {
    const p = `decoder_l${i}`;
    add(p + '_rnn_W', dim, dim); add(p + '_rnn_Wf', dim, dim); add(p + '_rnn_bf', 1, dim);
    norm(p + '_rnn_ffn_ln'); attention(p + '_context'); ffn(p + '_ffn');
  }
  add('decoder_l1_rnn_W_QuantMultA', 1);
  add('encoder_l1_self_Wq_QuantMultA', 1);
  return { weights: new Map([...weights].reverse()), config };
}

test.each([
  ['fp32', false], ['fp32', true], ['int4-k9', false], ['int4-k9', true],
  ['qdecoder', false], ['qdecoder', true],
] as const)('native SSRU rejects mode=%s shortlist=%s before reading weights', (mode, shortlist) => {
  assert.throws(() => buildModel(new Map(), {}, mode, shortlist, true), /nativeSsru.*qall.*shortlist/i);
});

test('native SSRU requires boolean shortlist and accepts the default qall mode', () => {
  for (const shortlist of [null, 0, 1, 'true']) assert.throws(() => buildModel(new Map(), {}, 'qall', shortlist as never, true), /nativeSsru.*qall.*shortlist/i);
  const { weights, config } = fixture();
  assert.deepEqual(buildModel(weights, config, undefined, true, true), buildModel(weights, config, 'qall', true, true));
  assert.deepEqual(buildModel(weights, config, undefined, false, true), buildModel(weights, config, 'qall', false, true));
  assert.deepEqual(buildModel(weights, config, 'qall', undefined, true), buildModel(weights, config, 'qall', false, true));
});

test('native SSRU full-vocab protobuf contract omits candidate_ids graph input and projection tables', () => {
  const { weights, config } = fixture(8, 2, 2, 2, true);
  const model = decode(buildModel(weights, config, 'qall', false, true)), g = model.graph;
  assert.equal(modelType.verify(model), null);
  assert.deepEqual(g.input.map((v: any) => v.name), ['X', 'eos_id']);
  assert(!g.initializer.some((t: any) => /^projection_|^shortlist_/.test(t.name)));
  const node = g.node.at(-1);
  assert.equal(node.opType, 'SsruTranslate');
  assert.deepEqual(node.input.slice(0, 2), ['X', 'eos_id']);
  const candInit = g.initializer.find((t: any) => t.name === node.input[2]);
  assert(candInit);
  assert.deepEqual(candInit.dims, [0]);
  assert.equal(candInit.dataType, 7);
});

test.each([
  ['dim-emb', 2], ['dim-emb', 5], ['dim-emb', 4.5], ['dim-emb', NaN],
  ['transformer-heads', 0], ['transformer-heads', 3], ['transformer-heads', 1.5],
  ['enc-depth', 0], ['dec-depth', 0], ['dec-depth', 1.5], ['transformer-ffn-depth', 0], ['transformer-ffn-depth', 1.5],
] as const)('native SSRU rejects invalid %s=%s', (key, n) => {
  const { weights, config } = fixture();
  assert.throws(() => buildModel(weights, { ...config, [key]: n }, 'qall', true, true), /Invalid nativeSsru model dimensions/);
});

test.each([
  [4, 1, 1, 1, true], [8, 2, 2, 2, false], [12, 3, 3, 3, true], [4, 2, 10, 1, false],
] as const)('native SSRU protobuf contract dim=%i heads=%i depth=%i ffn=%i tied=%s', (dim, heads, depth, ffnDepth, tied) => {
  const { weights, config } = fixture(dim, heads, depth, ffnDepth, tied);
  const model = decode(buildModel(weights, config, 'qall', true, true)), g = model.graph;
  assert.equal(modelType.verify(model), null);
  assert.deepEqual(model.opsetImport, [{ domain: '', version: 11 }, { domain: 'com.mtran', version: 1 }]);
  assert.deepEqual(g.input.map((v: any) => [v.name, v.type.tensorType.elemType, v.type.tensorType.shape.dim]), [
    ['X', 7, [{ dimParam: 'X_d0' }]], ['eos_id', 7, [{ dimValue: 1 }]], ['candidate_ids', 7, [{ dimParam: 'candidate_ids_d0' }]],
  ]);
  assert.deepEqual(g.output, [{ name: 'Y', type: { tensorType: { elemType: 7, shape: { dim: [{ dimParam: 'Y_d0' }] } } } }]);
  assert.equal(g.node.length, 1);
  const node = g.node[0], attrs = Object.fromEntries(node.attribute.map((a: any) => [a.name, a]));
  assert.equal(node.domain, 'com.mtran');
  assert.equal(node.opType, 'SsruTranslate'); assert.deepEqual(node.output, ['Y']);
  assert.deepEqual(Object.keys(attrs).sort(), [
    'dec_depth', 'dim', 'enc_depth', 'ffn_depth', 'heads',
    'int8_scale_names', 'int8_weight_names', 'tied_embeddings', 'weight_names',
  ]);
  for (const [name, n] of Object.entries({ dim, heads, enc_depth: 1, dec_depth: depth, ffn_depth: ffnDepth, tied_embeddings: tied ? 1 : 0 })) {
    assert.deepEqual(attrs[name], { name, type: 2, i: n });
  }
  assert.equal(attrs.weight_names.type, 8);
  assert.equal(attrs.int8_weight_names.type, 8);
  assert.equal(attrs.int8_scale_names.type, 8);
  const names = attrs.weight_names.strings.map((s: Buffer) => s.toString('utf8'));
  const emb = tied ? 'Wemb' : 'decoder_Wemb';
  const expectedNames = [...weights.keys()]
    .filter(name => !name.endsWith('_QuantMultA') && (name === emb || name === 'encoder_Wemb' || name === 'decoder_ff_logit_out_b' || /^(?:encoder|decoder)_l\d+_/.test(name)))
    .concat('position_frequency', 'position_offset')
    .sort();
  assert.deepEqual(names, expectedNames);
  assert(!names.some((name: string) => name.endsWith('_QuantMultA')));
  assert.equal(attrs.int8_weight_names.strings?.length ?? 0, 0);
  assert.equal(attrs.int8_scale_names.strings?.length ?? 0, 0);
  assert.deepEqual(node.input.slice(0, 3), ['X', 'eos_id', 'candidate_ids']);
  assert.equal(node.input.length, 3 + names.length);
  assert.deepEqual(node.input.slice(3), names);
  const tensors = new Map<string, any>(g.initializer.map((t: any) => [t.name, t]));
  assert(!g.initializer.some((t: any) => t.name.endsWith('_QuantMultA')));
  for (const name of names) {
    const tensor = tensors.get(name);
    assert(tensor, `missing constant ${name}`); assert.equal(tensor.dataType, 1);
    const original = weights.get(name);
    if (original) {
      const expected = original.values.slice();
      if (name === 'decoder_ff_logit_out_b') expected[1] = -Infinity;
      assert.deepEqual(tensor.dims, original.dims); assert.deepEqual(tensor.rawData, floatBytes(expected));
    } else {
      assert.deepEqual(tensor.dims, [dim]);
      const expected = Float32Array.from({ length: dim }, (_, i) => name === 'position_frequency' ? Math.pow(1e-4, (i % (dim / 2)) / (dim / 2 - 1)) : Math.floor(i / (dim / 2)) * Math.PI / 2);
      assert.deepEqual(tensor.rawData, floatBytes(expected));
    }
  }
  assert.notEqual(weights.get('decoder_ff_logit_out_b')!.values[1], -Infinity);
  assert(!g.initializer.some((t: any) => /^projection_|^shortlist_/.test(t.name)));
  assert(!g.node.some((n: any) => ['Loop', 'Concat', 'ArgMax', 'Sigmoid', 'Equal', 'Not', 'MatMulNBits'].includes(n.opType)));
  const known = new Set([...g.input.map((v: any) => v.name), ...tensors.keys()]);
  for (const n of g.node) {
    for (const name of n.input) assert(known.has(name), `${n.name} uses undefined ${name}`);
    for (const name of n.output) { assert(!known.has(name), `duplicate ${name}`); known.add(name); }
  }
});

test('native SSRU compact INT8 graph stores 0x4101 weights as INT8 + scalar scales and drops QuantMultA', () => {
  const { weights, config } = fixture(8, 2, 2, 2, false);
  for (const [name, w] of weights) {
    if (name === 'Wemb' || name === 'decoder_Wemb' || name === 'encoder_Wemb' || /_W(?:[qkvo]|f|\d+)?$/.test(name)) {
      const scale = 1 / 64;
      const int8 = new Int8Array(w.values.length);
      for (let i = 0; i < w.values.length; i++) {
        int8[i] = Math.max(-128, Math.min(127, Math.round(w.values[i] / scale)));
        w.values[i] = int8[i] * scale;
      }
      w.int8 = int8;
      w.int8Scale = scale;
    }
  }
  const model = decode(buildModel(weights, config, 'qall', true, true)), g = model.graph;
  assert.equal(modelType.verify(model), null);
  assert.equal(g.node.length, 1);
  const node = g.node[0];
  const attrs = Object.fromEntries(node.attribute.map((a: any) => [a.name, a]));
  const weightNames: string[] = attrs.weight_names.strings.map((s: Buffer) => s.toString('utf8'));
  const int8Names: string[] = attrs.int8_weight_names.strings.map((s: Buffer) => s.toString('utf8'));
  const scaleNames: string[] = attrs.int8_scale_names.strings.map((s: Buffer) => s.toString('utf8'));
  assert(int8Names.length > 0);
  assert.equal(int8Names.length, scaleNames.length);
  assert.deepEqual(node.input, ['X', 'eos_id', 'candidate_ids', ...weightNames, ...scaleNames]);
  const tensors = new Map<string, any>(g.initializer.map((t: any) => [t.name, t]));
  assert(!g.initializer.some((t: any) => t.name.endsWith('_QuantMultA')));
  for (let i = 0; i < int8Names.length; i++) {
    const name = int8Names[i], scaleName = scaleNames[i];
    assert(weightNames.includes(name));
    assert.equal(scaleName, `${name}_int8_scale`);
    const wt = tensors.get(name), st = tensors.get(scaleName);
    assert.equal(wt.dataType, 3);
    assert.deepEqual(wt.rawData, Buffer.from(weights.get(name)!.int8!.buffer));
    assert.equal(st.dataType, 1);
    assert.deepEqual(st.dims, undefined);
    assert.deepEqual(st.rawData, floatBytes(Float32Array.of(weights.get(name)!.int8Scale!)));
  }
  for (const name of weightNames) {
    if (!int8Names.includes(name)) assert.equal(tensors.get(name).dataType, 1);
  }
});

// SHA256s captured from the original converter before adding nativeSsru.
test.each([
  ['fp32', false, '152cad776f3c9f54ce39d01ef0c97ebf6803cc6c904a4968fad5f133eb75a95b'],
  ['fp32', true, '25789b13ba6535c2bdc2649d37fc8199773c52a468183583848f3939ab730058'],
  ['int4-k9', false, '656a60571a747e843b1801a60f8db9cb4a5421461fe5edfc720117d54f3a9e43'],
  ['int4-k9', true, '9e9e7ccfced5ad7d40187e9ad1571aba3471054f1e326eb879991f40044f21ad'],
  ['qdecoder', false, '4eab6c6c155d4c99267e6ed3e8861825abfa2bb3bdc8fab5d4ea63bc457f2735'],
  ['qdecoder', true, '609428f305cc27d88dc6ab1846afa351b5d87a195714e5380fb8272cc9fe0dd2'],
  ['qall', false, 'd2b905700b59e3f6f080deec53cc69265dcbd301ef1ed4ec4eb35319cc5eec13'],
  ['qall', true, '298b8398c475fe4c4362d1bda161de1cf36aeb8b8920a0c3240e450080e637ef'],
] as [Mode, boolean, string][])('legacy bytes remain unchanged mode=%s shortlist=%s', (mode, shortlist, digest) => {
  const { weights, config } = fixture();
  const original = buildModel(weights, config, mode, shortlist);
  assert.equal(createHash('sha256').update(original).digest('hex'), digest);
  assert.deepEqual(buildModel(weights, config, mode, shortlist, false), original);
  const model = decode(original);
  assert(!model.opsetImport.some((op: any) => op.domain === 'com.mtran'));
  assert.equal(model.graph.node.filter((n: any) => n.opType === 'Loop').length, 1);
});

test('readRelease preserves 0x4101 int8 data and scale alongside FP32 values', async () => {
  const dir = await mkdtemp(path.join(tmpdir(), 'mtran-rel-int8-'));
  try {
    const yml = Buffer.from(
      'type: transformer\ndec-cell: ssru\ntransformer-decoder-autoreg: rnn\ntransformer-ffn-activation: relu\n' +
      'transformer-postprocess: dan\ntied-embeddings-all: true\ndim-emb: 4\ntransformer-heads: 2\n' +
      'enc-depth: 1\ndec-depth: 1\ntransformer-ffn-depth: 1\n',
      'utf8',
    );
    const embBytes = Buffer.alloc(8 + 4);
    for (let i = 0; i < 8; i++) embBytes.writeInt8(i - 4, i);
    embBytes.writeFloatLE(4.0, 8); // inverse = 0.25
    const matBytes = Buffer.alloc(6 + 4);
    [1, 2, 3, -1, -2, -3].forEach((v, i) => matBytes.writeInt8(v, i));
    matBytes.writeFloatLE(2.0, 6); // inverse = 0.5
    const items = [
      { name: 'special:model.yml\0', kind: 0x101, dims: [yml.length], data: yml },
      { name: 'Wemb\0', kind: 0x4101, dims: [2, 4], data: embBytes },
      { name: 'decoder_l1_rnn_W\0', kind: 0x4101, dims: [3, 2], data: matBytes },
    ];
    const header = Buffer.alloc(16);
    header.writeBigUInt64LE(1n, 0);
    header.writeBigUInt64LE(BigInt(items.length), 8);
    const meta = Buffer.concat(items.map(item => {
      const b = Buffer.alloc(32);
      b.writeBigUInt64LE(BigInt(item.name.length), 0);
      b.writeBigUInt64LE(BigInt(item.kind), 8);
      b.writeBigUInt64LE(BigInt(item.dims.length), 16);
      b.writeBigUInt64LE(BigInt(item.data.length), 24);
      return b;
    }));
    const names = Buffer.concat(items.map(i => Buffer.from(i.name, 'utf8')));
    const dims = Buffer.concat(items.flatMap(i => i.dims.map(d => { const b = Buffer.alloc(4); b.writeInt32LE(d, 0); return b; })));
    const pad = Buffer.alloc(8);
    const file = path.join(dir, 'model.bin');
    await writeFile(file, Buffer.concat([header, meta, names, dims, pad, ...items.map(i => i.data)]));
    const { weights } = await readRelease(file);
    const emb = weights.get('Wemb')!;
    assert.deepEqual(Array.from(emb.int8!), [-4, -3, -2, -1, 0, 1, 2, 3]);
    assert.equal(emb.int8Scale, 0.25);
    assert.deepEqual(Array.from(emb.values), [-1, -0.75, -0.5, -0.25, 0, 0.25, 0.5, 0.75]);
    const mat = weights.get('decoder_l1_rnn_W')!;
    // Stored as [2, 3] in bin: row0=[1,2,3], row1=[-1,-2,-3] -> transposed to [3, 2]: [1,-1, 2,-2, 3,-3]
    assert.deepEqual(Array.from(mat.int8!), [1, -1, 2, -2, 3, -3]);
    assert.equal(mat.int8Scale, 0.5);
    assert.deepEqual(Array.from(mat.values), [0.5, -0.5, 1, -1, 1.5, -1.5]);
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});
