import { test, expect } from 'bun:test';
import { existsSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import protobuf from 'protobufjs';
import schema from '../src/converter/schema.json';

const root = path.resolve(import.meta.dir, '..');
const supported = ['linux', 'darwin'].includes(process.platform) && ['x64', 'arm64'].includes(process.arch);

// A real three-input model: Y = Gather(candidate_ids, X) + eos_id.
function model(shortlist = true) {
  const value = (name: string, dim: number | string) => ({ name, type: { tensorType: {
    elemType: 7, shape: { dim: [typeof dim === 'string' ? { dimParam: dim } : { dimValue: dim }] },
  } } });
  const type = protobuf.Root.fromJSON(schema).lookupType('onnx.ModelProto');
  return type.encode(type.create({ irVersion: 8, opsetImport: [{ domain: '', version: 13 }], graph: {
    name: 'native_async', input: shortlist ? [value('X', 'n'), value('candidate_ids', 'c'), value('eos_id', 1)] : [value('X', 'n'), value('eos_id', 1)],
    output: [value('Y', 'n')], node: shortlist ? [
      { opType: 'Gather', input: ['candidate_ids', 'X'], output: ['picked'] },
      { opType: 'Add', input: ['picked', 'eos_id'], output: ['Y'] },
    ] : [{ opType: 'Add', input: ['X', 'eos_id'], output: ['Y'] }],
  } })).finish();
}

test.skipIf(!supported)('native async session supports queued runs, draining close, GC and error recovery', async () => {
  mkdirSync(path.join(root, '.tmp'), { recursive: true });
  const directory = mkdtempSync(path.join(root, '.tmp/native-async-test-'));
  const compiler = (process.env.CXX ? [process.env.CXX] : ['c++', 'g++', 'clang++'])
    .find(command => spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0);
  const include = [process.env.NODE_INCLUDE_DIR, '/usr/include/node', '/usr/local/include/node',
    '/opt/homebrew/include/node', path.resolve(path.dirname(process.execPath), '../include/node')]
    .find(directory => directory && existsSync(path.join(directory, 'node_api.h')));
  if (!compiler || !include) {
    console.warn('Native tests skipped: compiler or Node headers unavailable');
    rmSync(directory, { recursive: true, force: true });
    return;
  }
  const binary = path.join(directory, 'production.node');
  const instrumented = path.join(directory, 'instrumented.node');
  for (const [target, defines] of [[binary, []], [instrumented, ['-DONNX_ASYNC_TEST']]] as const) {
    const build = Bun.spawn([compiler, '-std=c++20', '-O1', '-Wall', '-Wextra', '-shared', '-fPIC', '-pthread',
      '-I', include, '-I', path.join(root, 'native/onnx-async'), ...defines,
      path.join(root, 'native/onnx-async/addon.cc'),
      ...(process.platform === 'darwin' ? ['-undefined', 'dynamic_lookup'] : ['-ldl']), '-o', target],
      { cwd: root, stdout: 'pipe', stderr: 'pipe' });
    const [out, err, exit] = await Promise.all([
      new Response(build.stdout).text(), new Response(build.stderr).text(), build.exited,
    ]);
    expect(exit, out + err).toBe(0);
    expect(err).toBe('');
  }
  const fixture = path.join(directory, 'three-input.onnx');
  writeFileSync(fixture, model());
  const standardFixture = path.join(directory, 'two-input.onnx');
  writeFileSync(standardFixture, model(false));
  const ortRoot = path.dirname(require.resolve('onnxruntime-node/package.json'));
  const library = path.join(ortRoot, 'bin/napi-v6', process.platform, process.arch,
    process.platform === 'darwin' ? 'libonnxruntime.1.dylib' : 'libonnxruntime.so.1');
  const script = `
    const assert = require('node:assert/strict');
    const addon = require(${JSON.stringify(binary)});
    assert.equal(addon._testConfigure, undefined);
    const model = ${JSON.stringify(fixture)}, library = ${JSON.stringify(library)};
    (async () => {
      await assert.rejects(addon.init(model, library + '.missing'));
      await assert.rejects(addon.init(model + '.missing', library));
      for (const bad of [-1, 1.5, NaN, Infinity, 2 ** 32, '2'])
        await assert.rejects(addon.init(model, library, bad));
      const standard = await addon.init(${JSON.stringify(standardFixture)}, library, 2);
      await assert.rejects(addon.run(standard, BigInt64Array.of(0n), BigInt64Array.of(1n), 0n));
      const normalRuns = Array.from({length: 32}, (_, i) => addon.run(standard, BigInt64Array.of(2n, 0n), null, BigInt(i)));
      const standardClosing = addon.close(standard);
      for (const [i, result] of (await Promise.all(normalRuns)).entries())
        assert.deepEqual(result, BigInt64Array.of(2n + BigInt(i), BigInt(i)));
      await standardClosing;
      const initializing = addon.init(model, library);
      assert(initializing instanceof Promise);
      let handle = await initializing;
      const x = BigInt64Array.of(2n, 0n), candidates = BigInt64Array.of(10n, 20n, 30n);
      const run = (h = handle) => addon.run(h, x, candidates, 7n);
      const expected = BigInt64Array.of(37n, 17n);
      assert.deepEqual(await run(), expected);
      await assert.rejects(addon.run(handle, new Int32Array([0]), candidates, 0n));
      await assert.rejects(addon.run(handle, BigInt64Array.of(), candidates, 0n));
      await assert.rejects(addon.run(handle, x, candidates, 1n << 64n));
      await assert.rejects(addon.run({}, x, candidates, 0n));
      await assert.rejects(addon.run(handle, BigInt64Array.of(9n), candidates, 0n));
      assert.deepEqual(await run(), expected); // An ORT error cannot poison the shared session.
      const mutable = x.slice();
      const copied = addon.run(handle, mutable, candidates, 7n);
      mutable.fill(0n);
      assert.deepEqual(await copied, expected);
      let completed = 0;
      const pending = Array.from({ length: 64 }, (_, i) => {
        const eos = BigInt(i);
        return addon.run(handle, x, candidates, eos).then(output => {
          assert(output instanceof BigInt64Array);
          assert.deepEqual(output, BigInt64Array.of(30n + eos, 10n + eos)); completed++;
        });
      });
      const closing = addon.close(handle);
      assert(closing instanceof Promise);
      const drained = closing.then(() => assert.equal(completed, 64));
      const repeated = addon.close(handle);
      await assert.rejects(run(), /closing|closed/);
      await Promise.all([...pending, drained, repeated]);
      await addon.close(handle);
      await assert.rejects(run(), /closing|closed/);
      handle = null; global.gc();
      let orphan = await addon.init(model, library);
      const orphanJobs = Array.from({ length: 64 }, () => run(orphan));
      orphan = null;
      for (let i = 0; i < 3; i++) { global.gc(); await new Promise(r => setImmediate(r)); }
      for (const output of await Promise.all(orphanJobs)) assert.deepEqual(output, expected);
      const injected = require(${JSON.stringify(instrumented)});
      let faultHandle = await injected.init(model, library);
      const goodRun = () => injected.run(faultHandle, x, candidates, 7n);
      for (const method of ['init', 'run', 'close']) {
        injected._testConfigure('promise');
        const operation = method === 'init' ? () => injected.init(model, library)
          : method === 'run' ? goodRun : () => injected.close(faultHandle);
        assert.throws(operation, /N-API|promise/);
        assert.deepEqual(await goodRun(), expected);
      }
      for (const fault of ['async_work', 'queue']) {
        injected._testConfigure(fault);
        await assert.rejects(injected.init(model, library), /N-API|failure/);
        injected._testConfigure(fault);
        await assert.rejects(goodRun(), /N-API|failure/);
        assert.deepEqual(await goodRun(), expected);
      }
      await injected.close(faultHandle);
      console.log('native async fault injection checks passed');
      const { Worker } = require('node:worker_threads');
      const workerCode = \`
        const { parentPort, workerData } = require('node:worker_threads');
        const addon = require(workerData.binary);
        const control = new Int32Array(workerData.control);
        (async () => {
          if (workerData.phase === 'init') {
            addon._testConfigure('', control);
            addon.init(workerData.model, workerData.library).catch(() => {});
          } else {
            const handle = await addon.init(workerData.model, workerData.library);
            addon._testConfigure('', control);
            const count = workerData.phase === 'running' ? 1 : workerData.jobs;
            for (let i = 0; i < count; i++)
              addon.run(handle, BigInt64Array.of(0n), BigInt64Array.of(10n), 0n).catch(() => {});
            if (workerData.phase === 'close') addon.close(handle).catch(() => {});
          }
          parentPort.postMessage('submitted');
        })().catch(error => { throw error; });
      \`;
      const wait = async (condition, description) => {
        const deadline = Date.now() + 5000;
        while (!condition()) {
          if (Date.now() > deadline) throw new Error('Timeout: ' + description);
          await new Promise(resolve => setTimeout(resolve, 2));
        }
      };
      const pool = Number(process.env.UV_THREADPOOL_SIZE || 4);
      for (const phase of ['init', 'queued', 'running', 'close']) {
        const shared = new SharedArrayBuffer(6 * 4), control = new Int32Array(shared);
        const worker = new Worker(workerCode, { eval: true, workerData: {
          binary: ${JSON.stringify(instrumented)}, model, library, phase, control: shared, jobs: pool + 8,
        } });
        let failure;
        worker.on('error', error => { failure = error; });
        try {
          const expectedStarted = phase === 'init' || phase === 'running' ? 1 : pool;
          await wait(() => failure || Atomics.load(control, 0) >= expectedStarted, phase + ' execute');
          if (failure) throw failure;
          const terminated = worker.terminate();
          // Node may drain async_work before invoking addon cleanup hooks.
          // Releasing running work after terminate avoids a circular wait.
          Atomics.store(control, 1, 1);
          await terminated;
          assert.equal(Atomics.load(control, 2), 1, phase + ' cleanup hook');
          assert.equal(Atomics.load(control, 5), 0, phase + ' live jobs after teardown');
          assert(Atomics.load(control, 3) > 0, phase + ' completed native work');
          assert.equal(Atomics.load(control, 3), phase === 'init' || phase === 'running' ? 1 : pool + 8,
            phase + ' every submitted work completed or cancelled');
          console.log('native worker terminate passed: ' + phase);
        } finally {
          Atomics.store(control, 1, 1);
          await worker.terminate();
        }
      }
      console.log('native async synthetic checks passed');
    })().catch(error => { console.error(error); process.exitCode = 1; });
  `;
  try {
    const child = Bun.spawn(['node', '--expose-gc', '--eval', script], { cwd: root, stdout: 'pipe', stderr: 'pipe' });
    const [out, err, exit] = await Promise.all([
      new Response(child.stdout).text(), new Response(child.stderr).text(), child.exited,
    ]);
    expect(exit, out + err).toBe(0);
    expect(out).toContain('native async synthetic checks passed');
  } finally {
    rmSync(directory, { recursive: true, force: true });
  }
}, 60000);
