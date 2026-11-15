import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { test } from 'bun:test';
import protobuf from 'protobufjs';
import * as ort from 'onnxruntime-node';
import { buildModel, type Mode } from '../src/converter/graph';
import type { ModelConfig, Weight } from '../src/converter/release';
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
  assert.equal(node.opType, 'SsruDecode');
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
  assert.deepEqual(model.opsetImport, [{ domain: '', version: 11 }, { domain: 'com.microsoft', version: 1 }, { domain: 'com.mtran', version: 1 }]);
  assert.deepEqual(g.input.map((v: any) => [v.name, v.type.tensorType.elemType, v.type.tensorType.shape.dim]), [
    ['X', 7, [{ dimParam: 'X_d0' }]], ['eos_id', 7, [{ dimValue: 1 }]], ['candidate_ids', 7, [{ dimParam: 'candidate_ids_d0' }]],
  ]);
  assert.deepEqual(g.output, [{ name: 'Y', type: { tensorType: { elemType: 7, shape: { dim: [{ dimParam: 'Y_d0' }] } } } }]);
  const natives = g.node.filter((n: any) => n.domain === 'com.mtran');
  assert.equal(natives.length, 1);
  const node = natives[0], attrs = Object.fromEntries(node.attribute.map((a: any) => [a.name, a]));
  assert.equal(node, g.node.at(-1));
  assert.equal(node.opType, 'SsruDecode'); assert.deepEqual(node.output, ['Y']);
  assert.deepEqual(Object.keys(attrs).sort(), ['depth', 'dim', 'ffn_depth', 'heads', 'weight_names']);
  for (const [name, n] of Object.entries({ dim, heads, depth, ffn_depth: ffnDepth })) assert.deepEqual(attrs[name], { name, type: 2, i: n });
  assert.equal(attrs.weight_names.type, 8);
  const names = attrs.weight_names.strings.map((s: Buffer) => s.toString('utf8'));
  const emb = tied ? 'Wemb' : 'decoder_Wemb';
  assert.deepEqual(names, [...weights.keys()].filter(name => name === emb || name === 'decoder_ff_logit_out_b' || /^decoder_l\d+_/.test(name)).concat('position_frequency', 'position_offset').sort());
  assert.deepEqual(node.input.slice(0, 3), ['X', 'eos_id', 'candidate_ids']);
  assert.equal(node.input.length, 3 + depth * 2 + names.length);
  assert.deepEqual(node.input.slice(3 + depth * 2), names);
  const tensors = new Map<string, any>(g.initializer.map((t: any) => [t.name, t]));
  const producers = new Map<string, any>(g.node.flatMap((n: any) => n.output.map((s: string) => [s, n])));
  const attr = (n: any, name: string) => n.attribute.find((a: any) => a.name === name);
  for (let i = 0; i < depth; i++) {
    const key = producers.get(node.input[3 + i]), val = producers.get(node.input[3 + depth + i]);
    assert.equal(key.opType, 'Transpose'); assert.deepEqual(attr(key, 'perm').ints, [0, 1, 3, 2]);
    const keyHeads = producers.get(key.input[0]);
    for (const [n, letter] of [[keyHeads, 'k'], [val, 'v']] as const) {
      assert.equal(n.opType, 'Transpose'); assert.deepEqual(attr(n, 'perm').ints, [0, 2, 1, 3]);
      const reshape = producers.get(n.input[0]);
      assert.equal(reshape.opType, 'Reshape'); assert.deepEqual(tensors.get(reshape.input[1]).int64Data, [1, -1, heads, dim / heads]);
      const bias = producers.get(reshape.input[0]);
      assert.equal(bias.opType, 'Add'); assert.equal(bias.input[1], `decoder_l${i + 1}_context_b${letter}`);
    }
  }
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
  const quantized = g.node.filter((n: any) => n.opType === 'MatMulInteger').map((n: any) => n.input[1]).sort();
  const expectedQuantized = [...weights.keys()].filter(name => /^encoder_l1_(self_W[qkvo]|ffn_W\d+)$/.test(name) || /^decoder_l\d+_context_W[kv]$/.test(name)).map(name => name + '_quantized').sort();
  assert.deepEqual(quantized, expectedQuantized);
  assert(!g.initializer.some((t: any) => /^projection_|^shortlist_/.test(t.name)));
  assert(!g.node.some((n: any) => ['Loop', 'Concat', 'ArgMax', 'Sigmoid', 'Equal', 'Not', 'MatMulNBits'].includes(n.opType)));
  assert(!g.node.some((n: any) => n.attribute?.some((a: any) => a.g)));
  assert(!g.node.some((n: any) => n.input.includes('candidate_ids') && n !== node));
  const known = new Set([...g.input.map((v: any) => v.name), ...tensors.keys()]);
  for (const n of g.node) {
    for (const name of n.input) assert(known.has(name), `${n.name} uses undefined ${name}`);
    for (const name of n.output) { assert(!known.has(name), `duplicate ${name}`); known.add(name); }
  }
});

test.each([true, false])('native graph preserves executable encoder/KV for tied=%s without custom plugin', async tied => {
  const { weights, config } = fixture(8, 2, 2, 2, tied);
  const prepare = (native: boolean) => {
    const model = decode(buildModel(weights, config, 'qall', true, native)), g = model.graph;
    const after = (op: string, input: string) => g.node.find((n: any) => n.opType === op && n.input[0] === input).output[0];
    const kv = (i: number, letter: string) => {
      const bias = g.node.find((n: any) => n.opType === 'Add' && n.input[1] === `decoder_l${i}_context_b${letter}`).output[0];
      const heads = after('Transpose', after('Reshape', bias));
      return letter === 'k' ? after('Transpose', heads) : heads;
    };
    const outputs = ['encoder_hidden', kv(1, 'k'), kv(2, 'k'), kv(1, 'v'), kv(2, 'v')];
    if (native) assert.deepEqual(g.node.at(-1).input.slice(3, 7), outputs.slice(1));
    const used = new Set(outputs);
    for (const n of [...g.node].reverse()) if (n.output.some((name: string) => used.has(name))) n.input.forEach((name: string) => used.add(name));
    g.node = g.node.filter((n: any) => n.output.some((name: string) => used.has(name)));
    g.initializer = g.initializer.filter((t: any) => used.has(t.name));
    g.input = g.input.filter((v: any) => used.has(v.name));
    g.output = outputs.map(name => ({ name, type: { tensorType: { elemType: 1 } } }));
    return { bytes: modelType.encode(modelType.create(model)).finish(), outputs };
  };
  const reference = prepare(false), candidate = prepare(true);
  const options = { intraOpNumThreads: 1, interOpNumThreads: 1, logSeverityLevel: 3 as const };
  const baseline = await ort.InferenceSession.create(reference.bytes, options);
  let native: ort.InferenceSession | undefined;
  try {
    native = await ort.InferenceSession.create(candidate.bytes, options);
    for (const length of [1, 3, 7]) {
      const X = new ort.Tensor('int64', BigInt64Array.from({ length }, (_, i) => BigInt(i % 12)), [length]);
      let expected: ort.InferenceSession.ReturnType | undefined, actual: ort.InferenceSession.ReturnType | undefined;
      try {
        expected = await baseline.run({ X }); actual = await native.run({ X });
        for (let i = 0; i < reference.outputs.length; i++) {
          const a = actual[candidate.outputs[i]], b = expected[reference.outputs[i]];
          assert.deepEqual(a.dims, i === 0 ? [1, 1, length, 8] : i < 3 ? [1, 2, 4, length] : [1, 2, length, 4]);
          assert.deepEqual(a.dims, b.dims); assert.deepEqual(a.data, b.data);
        }
      } finally {
        X.dispose(); Object.values(expected ?? {}).forEach(t => t.dispose()); Object.values(actual ?? {}).forEach(t => t.dispose());
      }
    }
  } finally { await native?.release(); await baseline.release(); }
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
