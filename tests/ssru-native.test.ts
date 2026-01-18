import assert from 'node:assert/strict';
import { afterAll, beforeAll, test } from 'bun:test';
import { existsSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import path from 'node:path';
import protobuf from 'protobufjs';
import { buildModel } from '../src/converter/graph';
import type { Weight } from '../src/converter/release';
import schema from '../src/converter/schema.json';

const root = path.resolve(import.meta.dir, '..');
const supported = ['linux', 'darwin'].includes(process.platform) && ['x64', 'arm64'].includes(process.arch);
const compiler = supported ? (process.env.CXX ? [process.env.CXX] : ['c++', 'g++', 'clang++'])
  .find(command => spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0) : undefined;
const include = [process.env.NODE_INCLUDE_DIR, '/usr/include/node', '/usr/local/include/node',
  '/opt/homebrew/include/node', path.resolve(path.dirname(process.execPath), '../include/node')]
  .find(directory => directory && existsSync(path.join(directory, 'node_api.h')));
const ortRoot = path.dirname(require.resolve('onnxruntime-node/package.json'));
const library = path.join(ortRoot, 'bin/napi-v6', process.platform, process.arch,
  process.platform === 'darwin' ? 'libonnxruntime.1.dylib' : 'libonnxruntime.so.1');
const skipReason = !supported ? `unsupported ${process.platform}-${process.arch}` : !compiler ? 'C++ compiler unavailable'
  : !include ? 'Node headers unavailable (set NODE_INCLUDE_DIR)' : !existsSync(library) ? 'ORT native library unavailable'
  : spawnSync('node', ['--version'], { stdio: 'ignore' }).status !== 0 ? 'Node executable unavailable' : '';
if (skipReason) console.warn(`SSRU native integration tests skipped: ${skipReason}`);
const nativeTest = test.skipIf(!!skipReason);
const modelType = protobuf.Root.fromJSON(schema).lookupType('onnx.ModelProto');

// Zero decoder linears preserve positional signs: token 3 at position 0, EOS 2
// at position 1. Tokens 3/4 tie; token 1 has a large bias but must stay masked.
function fixture(tied: boolean) {
  const dim = 4, weights = new Map<string, Weight>();
  const add = (name: string, dims: number[], diagonal = false) => {
    const values = new Float32Array(dims.reduce((a, b) => a * b, 1));
    if (name.endsWith('_scale')) values.fill(1);
    if (diagonal) for (let i = 0; i < dim; i++) values[i * dim + i] = .5;
    weights.set(name, { dims, values });
  };
  const norm = (p: string) => { add(p + '_scale', [1, dim]); add(p + '_bias', [1, dim]); };
  const attention = (p: string, encoder: boolean) => {
    for (const letter of ['q', 'k', 'v', 'o']) {
      add(p + '_W' + letter, [dim, dim], encoder || letter === 'k' || letter === 'v');
      add(p + '_b' + letter, [1, dim]);
    }
    norm(p + '_Wo_ln');
  };
  const ffn = (p: string) => {
    add(p + '_W1', [dim, dim * 2]); add(p + '_b1', [1, dim * 2]);
    add(p + '_W2', [dim * 2, dim]); add(p + '_b2', [1, dim]); norm(p + '_ffn_ln');
  };
  const emb = tied ? 'Wemb' : 'decoder_Wemb';
  add(emb, [12, dim]); weights.get(emb)!.values[2 * dim] = 1;
  if (!tied) add('encoder_Wemb', [13, dim]);
  add('decoder_ff_logit_out_b', [1, 12]);
  const bias = weights.get('decoder_ff_logit_out_b')!.values;
  bias.fill(-100); bias[1] = 1000; bias[2] = bias[3] = bias[4] = 0;
  attention('encoder_l1_self', true); ffn('encoder_l1_ffn');
  for (let i = 1; i <= 2; i++) {
    const p = `decoder_l${i}`;
    add(p + '_rnn_W', [dim, dim]); add(p + '_rnn_Wf', [dim, dim]); add(p + '_rnn_bf', [1, dim]);
    norm(p + '_rnn_ffn_ln'); attention(p + '_context', false); ffn(p + '_ffn');
  }
  const config = { 'dim-emb': dim, 'transformer-heads': 2, 'enc-depth': 1, 'dec-depth': 2, 'transformer-ffn-depth': 2, 'tied-embeddings-all': tied };
  return { weights, config };
}

let directory = '', binary = '', probeBinary = '';
const files: Record<string, string> = {};
const malformed: { name: string; file: string; pattern: string }[] = [];
const kvCases: { name: string; file: string; pattern: string; validAtTwo: boolean }[] = [];

beforeAll(async () => {
  if (skipReason) return;
  mkdirSync(path.join(root, '.tmp'), { recursive: true });
  directory = mkdtempSync(path.join(root, '.tmp/ssru-native-test-'));
  binary = path.join(directory, 'ssru.node');
  const child = Bun.spawn([compiler!, '-std=c++20', '-O1', '-Wall', '-Wextra', '-shared', '-fPIC', '-pthread',
    '-I', include!, '-I', path.join(root, 'native/onnx-async'), '-DMTRAN_NATIVE_SSRU',
    path.join(root, 'native/onnx-async/addon.cc'), path.join(root, 'native/onnx-async/ssru_op.cc'),
    path.join(root, 'native/ssru/ssru.cc'),
    ...(process.platform === 'darwin' ? ['-undefined', 'dynamic_lookup'] : ['-ldl']), '-o', binary],
  { cwd: root, stdout: 'pipe', stderr: 'pipe' });
  const timer = setTimeout(() => child.kill(), 60_000);
  try {
    const [out, err, exit] = await Promise.all([new Response(child.stdout).text(), new Response(child.stderr).text(), child.exited]);
    assert.equal(exit, 0, out + err);
  } finally { clearTimeout(timer); }
  probeBinary = path.join(directory, 'kleidi-test');
  const probe = Bun.spawn([compiler!, '-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-pthread',
    path.join(root, 'native/onnx-async/kleidi_test.cc'), path.join(root, 'native/ssru/ssru.cc'),
    ...(process.platform === 'darwin' ? [] : ['-ldl']), '-o', probeBinary],
  { cwd: root, stdout: 'pipe', stderr: 'pipe' });
  const [probeOut, probeErr, probeExit] = await Promise.all([
    new Response(probe.stdout).text(), new Response(probe.stderr).text(), probe.exited]);
  assert.equal(probeExit, 0, probeOut + probeErr);
  const save = (name: string, bytes: Uint8Array) => {
    const file = path.join(directory, name + '.onnx'); writeFileSync(file, bytes); files[name] = file; return file;
  };
  let bytes!: Uint8Array;
  for (const tied of [true, false]) {
    const { weights, config } = fixture(tied), name = tied ? 'shared' : 'separate';
    save(name + '-fast', buildModel(weights, config, 'qall', true));
    const native = buildModel(weights, config, 'qall', true, true);
    save(name + '-native', native); if (tied) bytes = native;
  }
  const mutate = (name: string, change: (g: any, n: any) => void) => {
    const model = modelType.toObject(modelType.decode(Buffer.from(bytes)), { longs: Number, bytes: Buffer }) as any;
    change(model.graph, model.graph.node.at(-1));
    assert.equal(modelType.verify(model), null, name);
    return save(name, modelType.encode(modelType.create(model)).finish());
  };
  const bad = (name: string, pattern: string, change: (g: any, n: any) => void) => malformed.push({ name, pattern, file: mutate(name, change) });
  const attr = (n: any, name: string) => n.attribute.find((a: any) => a.name === name);
  const tensor = (g: any, name: string) => g.initializer.find((t: any) => t.name === name);
  bad('missing-attribute', 'attribute|No attribute', (_g, n) => { n.attribute = n.attribute.filter((a: any) => a.name !== 'dim'); });
  for (const [name, value] of [['dim', 2], ['heads', 3], ['depth', 0], ['ffn_depth', 0]] as const)
    bad('invalid-' + name, 'SSRU.*(configuration|count)', (_g, n) => { attr(n, name).i = value; });
  bad('weight-count', 'SSRU.*count', (_g, n) => { attr(n, 'weight_names').strings.pop(); });
  bad('empty-weight-names', 'SSRU.*weight count', (_g, n) => { attr(n, 'weight_names').strings = []; });
  for (const name of ['decoder_l1_rnn_W', 'position_offset'])
    bad('duplicate-' + name, '[Dd]uplicate.*weight', (_g, n) => {
      const names = attr(n, 'weight_names').strings;
      const index = names.findIndex((s: Buffer) => s.toString() === name);
      names[index] = Buffer.from(name === 'position_offset' ? 'position_frequency' : 'decoder_l1_rnn_Wf');
    });
  bad('missing-core-weight', 'SSRU missing weight', (_g, n) => {
    const names = attr(n, 'weight_names').strings;
    names[names.findIndex((s: Buffer) => s.toString() === 'decoder_l1_rnn_bf')] = Buffer.from('unused_bias');
  });
  bad('matrix-shape', 'SSRU invalid matrix', (g) => { tensor(g, 'decoder_l1_rnn_W').dims = [2, 8]; });
  bad('bias-shape', 'SSRU invalid output bias', (g) => { tensor(g, 'decoder_ff_logit_out_b').dims = [2, 6]; });
  bad('nonfinite-weight', '[Nn]on.?finite.*(weight|matrix)', (g) => { tensor(g, 'decoder_l1_rnn_W').rawData.writeFloatLE(NaN, 0); });
  bad('nonfloat-weight', 'Type.*(Error|invalid)|type.*(float|tensor)', (g) => { tensor(g, 'decoder_l1_rnn_W').dataType = 6; });
  for (const keepInitializer of [true, false])
    bad('overridable-weight-' + keepInitializer, 'SSRU weights must be immutable initializers', (g) => {
      const name = 'decoder_l1_rnn_W';
      if (!keepInitializer) g.initializer = g.initializer.filter((t: any) => t.name !== name);
      g.input.push({ name, type: { tensorType: { elemType: 1, shape: { dim: [{ dimValue: 4 }, { dimValue: 4 }] } } } });
    });
  bad('computed-weight', 'SSRU weights must be immutable initializers', (g, n) => {
    const weight = tensor(g, 'decoder_l1_rnn_W'); weight.name += '_base';
    g.node.splice(g.node.indexOf(n), 0,
      { name: 'cast_weight_input', opType: 'Cast', input: ['X'], output: ['weight_float'], attribute: [{ name: 'to', type: 2, i: 1 }] },
      { name: 'sum_weight_input', opType: 'ReduceSum', input: ['weight_float'], output: ['weight_sum'], attribute: [{ name: 'axes', type: 7, ints: [0] }, { name: 'keepdims', type: 2, i: 0 }] },
      { name: 'dynamic_weight', opType: 'Add', input: [weight.name, 'weight_sum'], output: ['decoder_l1_rnn_W'] });
  });
  for (const [name, index] of [['keys', 3], ['values', 5]] as const) {
    for (const nonfinite of [false, true]) {
      const label = name + (nonfinite ? '-nonfinite' : '-shape');
      const file = mutate(label, (g, n) => {
        const rawData = Buffer.alloc(8 * 4);
        if (nonfinite) rawData.writeFloatLE(Infinity, 0);
        g.initializer.push({ name: label, dims: [1, 2, 2, 2], dataType: 1, rawData });
        n.input[index] = label;
      });
      kvCases.push({ name: label, file, pattern: nonfinite ? 'SSRU nonfinite value' : 'SSRU invalid K/V dimensions', validAtTwo: !nonfinite });
    }
  }
}, 90_000);

afterAll(() => { if (directory) rmSync(directory, { recursive: true, force: true }); });

nativeTest('SSRU quantization probe detects real ORT arithmetic and rejects unknown or failed probes', () => {
  const result = spawnSync(probeBinary, [library], { cwd: root, encoding: 'utf8', timeout: 30_000 });
  assert.equal(result.status, 0, result.error?.message || result.stdout + result.stderr);
  assert.match(result.stdout, /PASS: quantization probe.*disabled configs and failure rejection/);
});

async function runNode(body: string) {
  const script = `
    const assert = require('node:assert/strict');
    const ort = require('onnxruntime-node');
    const addon = require(${JSON.stringify(binary)});
    const files = ${JSON.stringify(files)}, library = ${JSON.stringify(library)};
    const malformed = ${JSON.stringify(malformed)}, kvCases = ${JSON.stringify(kvCases)};
    const tokens = xs => BigInt64Array.from(xs, BigInt);
    const run = (handle, x = [0, 2], ids = [1, 2, 3, 4], eos = 2) => addon.run(handle, tokens(x), tokens(ids), BigInt(eos));
    const initialize = file => addon.init(file, library, 1);
    const reference = async (session, x, ids, eos) => {
      const feeds = { X: new ort.Tensor('int64', tokens(x), [x.length]), eos_id: new ort.Tensor('int64', tokens([eos]), [1]), candidate_ids: new ort.Tensor('int64', tokens(ids), [ids.length]) };
      let result;
      try { result = await session.run(feeds); return Array.from(result.Y.data, Number); }
      finally { Object.values(feeds).forEach(t => t.dispose()); Object.values(result || {}).forEach(t => t.dispose()); }
    };
    (async () => { assert.equal(addon.ssruVersion, 1); ${body} })().catch(error => { console.error(error); process.exitCode = 1; });
  `;
  const child = Bun.spawn(['node', '--unhandled-rejections=strict', '--eval', script], {
    cwd: root, env: { ...process.env, UV_THREADPOOL_SIZE: '4' }, stdout: 'pipe', stderr: 'pipe',
  });
  const timer = setTimeout(() => child.kill(), 45_000);
  try {
    const [out, err, exit] = await Promise.all([new Response(child.stdout).text(), new Response(child.stderr).text(), child.exited]);
    assert.equal(exit, 0, out + err);
  } finally { clearTimeout(timer); }
}

nativeTest('SSRU addon matches ordinary Fast ORT with defined EOS, token ties and length limits', async () => {
  await runNode(`
    for (const name of ['shared', 'separate']) {
      const session = await ort.InferenceSession.create(files[name + '-fast'], { intraOpNumThreads: 1, interOpNumThreads: 1, logSeverityLevel: 3 });
      let native, ordinary;
      try {
        native = await initialize(files[name + '-native']); ordinary = await initialize(files[name + '-fast']);
        const cases = [
          [[0, 2], [1, 2, 3, 4], 2, [3, 2]],
          [[2], [1, 2], 2, [2]],
          [[0, 0, 2], [4, 3, 2, 1], 2, [4, 2]],
          [[0, 2], [4, 3], 4, [4]],
          [[0, 2], [3, 4], 3, [3]],
          [[0, 2], [3, 3, 4], 3, [3]],
          [[0, 2], [4, 3], 3, Array(7).fill(4)],
          [[0, 0, 0, 2], [3], 2, Array(13).fill(3)],
        ];
        for (const [x, ids, eos, expected] of cases) {
          assert.deepEqual(await reference(session, x, ids, eos), expected);
          assert.deepEqual(Array.from(await run(ordinary, x, ids, eos), Number), expected);
          assert.deepEqual(Array.from(await run(native, x, ids, eos), Number), expected);
        }
      } finally { if (native) await addon.close(native); if (ordinary) await addon.close(ordinary); await session.release(); }
    }
  `);
}, 60_000);

nativeTest('SSRU initialization rejects malformed attrs, weight names, shapes and mutable weights', async () => {
  await runNode(`
    for (const item of malformed) {
      let unexpected;
      try {
        await assert.rejects(async () => { unexpected = await initialize(item.file); }, new RegExp(item.pattern, 'i'), item.name);
      } finally { if (unexpected) await addon.close(unexpected); }
      const handle = await initialize(files['shared-native']);
      try { assert.deepEqual(await run(handle), tokens([3, 2]), item.name + ' recovery'); }
      finally { await addon.close(handle); }
    }
  `);
}, 60_000);

nativeTest('SSRU validates runtime K/V, EOS and candidate inputs without poisoning later runs', async () => {
  await runNode(`
    const handle = await initialize(files['shared-native']);
    try {
      const bad = [
        () => run(handle, [], [2]), () => run(handle, [0], []),
        () => run(handle, [0], [-1, 2]), () => run(handle, [0], [2, 12]),
        () => run(handle, [0], [2], -1), () => run(handle, [0], [2], 12),
        () => addon.run(handle, tokens([0]), tokens([2]), 1n << 64n),
        () => addon.run(handle, new Int32Array([0]), tokens([2]), 2n),
        () => addon.run(handle, tokens([0]), new Int32Array([2]), 2n),
        () => run(handle, [99], [2]),
      ];
      for (const operation of bad) {
        await assert.rejects(operation);
        assert.deepEqual(await run(handle), tokens([3, 2]));
      }
    } finally { await addon.close(handle); }
    for (const item of kvCases) {
      const session = await initialize(item.file);
      try {
        if (item.validAtTwo) assert.deepEqual(await run(session), tokens([3, 2]));
        await assert.rejects(run(session, item.validAtTwo ? [0, 0, 2] : [0, 2]), new RegExp(item.pattern), item.name);
        if (item.validAtTwo) assert.deepEqual(await run(session), tokens([3, 2]));
      } finally { await addon.close(session); }
    }
  `);
}, 60_000);

nativeTest('SSRU isolates heterogeneous concurrent requests and drains both success and failure before close', async () => {
  await runNode(`
    let handle = await initialize(files['shared-native']);
    try {
      const x = tokens([0, 0, 2]), ids = tokens([4, 3, 2]);
      const copied = addon.run(handle, x, ids, 2n);
      x.fill(99n); ids.fill(99n);
      assert.deepEqual(await copied, tokens([4, 2]));
      const requests = Array.from({ length: 32 }, (_, i) => {
        const x = Array(1 + i % 7).fill(0); x.push(2);
        return i % 4 === 0 ? [x, [1, 2], 2, [2]]
          : i % 4 === 1 ? [x, [4, 3, 2], 2, [4, 2]]
          : i % 4 === 2 ? [x, [3, 4], 3, [3]]
          : [x, [4, 3], 3, Array(1 + 3 * x.length).fill(4)];
      });
      let settled = 0;
      const jobs = requests.map(async ([x, ids, eos, expected]) => {
        try { assert.deepEqual(await run(handle, x, ids, eos), tokens(expected)); }
        finally { settled++; }
      });
      for (let i = 0; i < 8; i++) jobs.push(run(handle, [0, 2], [12, 2]).then(
        () => { settled++; assert.fail('invalid candidate unexpectedly accepted'); },
        error => { settled++; assert.match(error.message, /candidate outside vocabulary/); },
      ));
      const closing = addon.close(handle);
      assert(closing instanceof Promise);
      const drained = closing.then(() => assert.equal(settled, 40));
      const repeated = addon.close(handle);
      await assert.rejects(run(handle), /closing|closed/);
      await Promise.all([...jobs, drained, repeated]);
      await addon.close(handle);
      await assert.rejects(run(handle), /closing|closed/);
    } finally { await addon.close(handle); }
    handle = await initialize(files['shared-native']);
    try { assert.deepEqual(await run(handle), tokens([3, 2])); }
    finally { await addon.close(handle); }
  `);
}, 60_000);
