import { afterEach, expect, mock, spyOn, test } from 'bun:test';
import { fileURLToPath } from 'node:url';
import * as ort from 'onnxruntime-node';
import protobuf from 'protobufjs';
import * as backend from '../src/core/backend';
import * as native from '../src/core/native';
import * as ssruCache from '../src/converter/ssru-cache';
import { TranslationEngine } from '../src/core/engine';
import { Tokenizer } from '../src/core/tokenizer';
import { Shortlist, checksum, generateCandidates, parseLexicon } from '../src/core/shortlist';
import { mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import path from 'node:path';

const oldCpuOnly = process.env.MT_CPU_ONLY;
const oldNativeSsru = process.env.MT_NATIVE_SSRU;
let tempDir: string | undefined;

afterEach(async () => {
  mock.restore();
  if (oldCpuOnly === undefined) delete process.env.MT_CPU_ONLY;
  else process.env.MT_CPU_ONLY = oldCpuOnly;
  if (oldNativeSsru === undefined) delete process.env.MT_NATIVE_SSRU;
  else process.env.MT_NATIVE_SSRU = oldNativeSsru;
  if (tempDir) await rm(tempDir, { recursive: true, force: true });
  tempDir = undefined;
});

async function isolated(script: string) {
  const child = Bun.spawn([process.execPath, '--eval', script], {
    cwd: fileURLToPath(new URL('..', import.meta.url)),
    stdout: 'pipe',
    stderr: 'pipe',
  });
  const [stdout, stderr, code] = await Promise.all([
    new Response(child.stdout).text(),
    new Response(child.stderr).text(),
    child.exited,
  ]);
  expect(code, stdout + stderr).toBe(0);
}

function makeVocab(size = 128, eos = 2, byteIds = [7, 30, 60, 110]) {
  const w = protobuf.Writer.create();
  for (let id = 0; id < size; id++) {
    const piece = protobuf.Writer.create().uint32(10).string(`p${id}`)
      .uint32(24).int32(byteIds.includes(id) ? 6 : 1).finish();
    w.uint32(10).bytes(piece);
  }
  return w.uint32(18).bytes(protobuf.Writer.create().uint32(336).int32(eos).finish()).finish();
}

function makeLex(sourceSize: number, targetSize: number, first: number) {
  const offsets = new Array<number>(sourceSize + 1).fill(0);
  const targets: number[] = [];
  for (let s = 0; s < sourceSize; s++) {
    offsets[s] = targets.length;
    targets.push((s * 13 + 3) % targetSize, (s * 37 + 42) % targetSize, (s * 7 + 80) % targetSize);
  }
  offsets[sourceSize] = targets.length;
  const buf = Buffer.alloc(48 + offsets.length * 8 + targets.length * 4);
  buf.writeBigUInt64LE(0xF11A48D5013417F5n, 0);
  buf.writeBigUInt64LE(BigInt(first), 16);
  buf.writeBigUInt64LE(2n, 24);
  buf.writeBigUInt64LE(BigInt(offsets.length), 32);
  buf.writeBigUInt64LE(BigInt(targets.length), 40);
  offsets.forEach((n, i) => buf.writeBigUInt64LE(BigInt(n), 48 + i * 8));
  targets.forEach((id, i) => buf.writeUInt32LE(id, 48 + offsets.length * 8 + i * 4));
  buf.writeBigUInt64LE(checksum(buf.subarray(16)), 8);
  return buf;
}

test('Shortlist baseIds precomputation matches generateCandidates with sparse bytes across edge prefixes', async () => {
  await mkdir('.tmp', { recursive: true });
  tempDir = await mkdtemp(path.resolve('.tmp/shortlist-base-'));
  const byteIds = [7, 30, 60, 120, 4000];
  const srcVocab = path.join(tempDir, 'src.spm');
  const dstVocab = path.join(tempDir, 'dst.spm');
  const lexPath = path.join(tempDir, 'lex.bin');
  await writeFile(srcVocab, makeVocab(1024, 2, byteIds));
  await writeFile(dstVocab, makeVocab(8192, 2, byteIds));

  for (const shared of [false, true]) {
    const targetSize = shared ? 1024 : 8192;
    const lexBytes = makeLex(1024, targetSize, 50);
    await writeFile(lexPath, lexBytes);
    const lex = parseLexicon(lexBytes, targetSize, 1024);
    const shortlist = await Shortlist.load(lexPath, srcVocab, shared ? srcVocab : dstVocab);
    const expectedIds = shared ? byteIds.filter(id => id < targetSize) : byteIds;
    for (const ids of [[2], [5, 19, 31], [49, 50, 51, 500, 1023], [0, 7, 30, 60, 200, 999]]) {
      const expected = generateCandidates(lex, ids, targetSize, shared, 2, expectedIds);
      expect(shortlist.candidates(ids)).toEqual(expected);
      expect(Array.from(shortlist.candidatesBigInt(BigInt64Array.from(ids, BigInt)), Number)).toEqual(expected);
    }
    expect(() => shortlist.candidates([])).toThrow(/source/);
    expect(() => shortlist.candidates([-1])).toThrow(/source/);
    expect(() => shortlist.candidates([1024])).toThrow(/source/);
  }
});

test('Tokenizer retains/closes idempotently and TranslationEngine deduplicates shared vocabularies', async () => {
  const { SentencePieceProcessor } = await import('@sctg/sentencepiece-js');
  let deletes = 0;
  spyOn(SentencePieceProcessor.prototype, 'loadFromB64StringModel').mockImplementation(async function (this: any) {
    this.processor = {
      status: () => ({ ToString: () => 'OK', delete() {} }),
      delete() { deletes++; },
    };
  });
  spyOn(SentencePieceProcessor.prototype, 'encodeIds').mockReturnValue([3, 4]);
  spyOn(SentencePieceProcessor.prototype, 'decodeIds').mockReturnValue('ok');
  await mkdir('.tmp', { recursive: true });
  tempDir = await mkdtemp(path.resolve('.tmp/tok-dedup-'));
  const vocab = path.join(tempDir, 'shared.spm');
  await writeFile(vocab, makeVocab(64, 2, [5]));

  const tok = await Tokenizer.load(vocab);
  tok.retain();
  tok.close();
  expect(deletes).toBe(0);
  tok.close();
  expect(deletes).toBe(1);
  tok.close();
  expect(deletes).toBe(1);

  deletes = 0;
  const loadSpy = spyOn(Tokenizer, 'load');
  process.env.MT_CPU_ONLY = 'true';
  process.env.MT_NATIVE_SSRU = '0';
  const session = {
    inputNames: ['X', 'eos_id'],
    run: async () => ({ Y: new ort.Tensor('int64', BigInt64Array.of(3n, 2n), [2]) }),
    release: mock(async () => {}),
  };
  spyOn(backend, 'selectBackend').mockImplementation(async (_m, probe) => {
    await probe(session as any);
    return {
      session,
      info: { provider: 'cpu', cpuThreads: 1, tuning: {} as any, maxConcurrentRuns: 1, measurements: [] },
      fallbackCpuThreads: 1,
      fallbackCpuTuning: {} as any,
    } as any;
  });
  spyOn(native, 'createNativeSession').mockResolvedValue(undefined);

  const engine = new TranslationEngine();
  await engine.init({ directory: tempDir, identity: 'id', model: 'm.onnx', sourceVocab: vocab, targetVocab: vocab });
  expect(loadSpy).toHaveBeenCalledTimes(1);
  await engine.destroy();
  expect(deletes).toBe(1);
});

test('TranslationEngine skips model.onnx double-load in MT_CPU_ONLY native-ssru mode and falls back on error', async () => {
  process.env.MT_CPU_ONLY = 'true';
  process.env.MT_NATIVE_SSRU = '1';
  const src = { size: 64, eos: 2, encode: () => [4, 5], decode: () => 'ok', retain() { return this; }, close: mock(() => {}) };
  spyOn(Tokenizer, 'load').mockResolvedValue(src as any);
  spyOn(Shortlist, 'load').mockResolvedValue({ candidatesBigInt: () => BigInt64Array.of(0n, 2n, 4n, 5n) } as any);
  spyOn(native, 'hasNativeSsru').mockReturnValue(true);
  spyOn(ssruCache, 'ensureNativeSsru').mockResolvedValue({ model: 'ssru.onnx', identity: 'ssru-id' });
  const ssruSession = {
    inputNames: ['X', 'eos_id', 'candidate_ids'],
    run: mock(async () => ({ Y: new ort.Tensor('int64', BigInt64Array.of(4n, 2n), [2]) })),
    release: mock(async () => {}),
  };
  const createSpy = spyOn(native, 'createNativeSession').mockResolvedValue(ssruSession as any);
  const selectSpy = spyOn(backend, 'selectBackend');

  const model = { directory: '.', identity: 'id', model: 'ordinary.onnx', sourceModel: 'src.bin', sourceVocab: 'v', targetVocab: 'v', lex: 'lex' };
  const engine = new TranslationEngine();
  await engine.init(model);
  expect(selectSpy).not.toHaveBeenCalled();
  expect(createSpy).toHaveBeenCalledWith('ssru.onnx', ['X', 'eos_id', 'candidate_ids'], 1, true);
  expect(engine.backend?.runtime).toBe('native-ssru');
  await engine.destroy();

  const fallbackSession = {
    inputNames: ['X', 'eos_id', 'candidate_ids'],
    run: mock(async () => ({ Y: new ort.Tensor('int64', BigInt64Array.of(4n, 2n), [2]) })),
    release: mock(async () => {}),
  };
  createSpy.mockRejectedValueOnce(new Error('bad ssru'));
  selectSpy.mockImplementation(async (_file, probe) => {
    await probe(fallbackSession as any);
    return {
      session: fallbackSession,
      info: { provider: 'cpu', cpuThreads: 1, tuning: {} as any, maxConcurrentRuns: 1, measurements: [] },
      fallbackCpuThreads: 1,
      fallbackCpuTuning: {} as any,
    } as any;
  });
  process.env.MT_NATIVE_SSRU = '1';
  const engine2 = new TranslationEngine();
  await engine2.init(model);
  expect(selectSpy).toHaveBeenCalledTimes(1);
  await engine2.destroy();
});

test('TranslationEngine runBatch length-buckets LONG_DOC, HTML nodes and translateBatchAsync with NativeRunError fallback', async () => {
  const engine = new TranslationEngine() as any;
  const decoded = new Map<number, string>();
  let nextTok = 10;
  engine.tokenLimit = 40;
  engine.modelPath = 'fallback.onnx';
  engine.fallbackCpuThreads = 1;
  engine.backend = { provider: 'cpu', runtime: 'native-ssru', maxConcurrentRuns: 8, tuning: { spinning: false } };
  engine.src = {
    eos: 0,
    size: 8192,
    encode(text: string) {
      const toks = text.trim().split(/\s+/).map(word => {
        const id = nextTok++;
        decoded.set(id, word);
        return id;
      });
      return toks;
    },
    close() {},
  };
  engine.dst = {
    eos: 0,
    size: 8192,
    decode(ids: Int32Array) {
      return [...ids].map(id => `[${decoded.get(id)}]`).join(' ');
    },
    close() {},
  };

  const batchSizes: number[] = [];
  const batchLengths: number[][] = [];
  const nativeSession = {
    inputNames: ['X', 'eos_id'],
    runBatch: mock(async (inputs: BigInt64Array[], _cands: any, eos: bigint) => {
      batchSizes.push(inputs.length);
      batchLengths.push(inputs.map(arr => arr.length));
      return inputs.map(inp => BigInt64Array.from([...inp.slice(0, -1), eos]));
    }),
    run: mock(async () => ({ Y: new ort.Tensor('int64', BigInt64Array.of(0n), [1]) })),
    release: mock(async () => {}),
  };
  engine.session = nativeSession;
  engine.nativeSessions.add(nativeSession);

  const sentences = Array.from({ length: 12 }, (_, i) => `Sentence ${i}${' word'.repeat((12 - i) % 4)}. `);
  const longDoc = sentences.join('');
  const translatedLong = await engine.translateAsync(longDoc);
  expect(batchSizes).toEqual([8, 4]);
  // Verify ascending length sorting inside the micro-batch.
  for (const lens of batchLengths) {
    for (let i = 1; i < lens.length; i++) expect(lens[i]).toBeGreaterThanOrEqual(lens[i - 1]);
  }
  expect(translatedLong).toContain('[Sentence] [0.]');
  expect(translatedLong).toContain('[Sentence] [11] [word.]');

  const htmlRes = await engine.translateAsync('<div><p>Alpha &amp; Beta</p><span>Gamma</span></div>', { html: true });
  expect(htmlRes).toBe('<div><p>[Alpha] [&amp;] [Beta]</p><span>[Gamma]</span></div>');

  const batchOut = await engine.translateBatchAsync(['  one {0} two  ', 'three', '']);
  expect(batchOut).toEqual(['  [one] {0} [two]  ', '[three]', '']);

  // Verify NativeRunError triggers CPU session fallback during runBatch.
  nativeSession.runBatch.mockRejectedValueOnce(new native.NativeRunError('boom'));
  const cpuSession = {
    inputNames: ['X', 'eos_id'],
    run: mock(async (feeds: any) => ({ Y: new ort.Tensor('int64', feeds.X.data, [feeds.X.data.length]) })),
    release: mock(async () => {}),
  };
  spyOn(backend, 'createSession').mockResolvedValue(cpuSession as any);
  const recovered = await engine.translateBatchAsync(['hello world', 'foo']);
  expect(recovered).toEqual(['[hello] [world]', '[foo]']);
  expect(engine.backend.runtime).toBeUndefined();
  await engine.destroy();
});

test('createNativeSession.runBatch supports both binding.runBatch and Promise.all fallback', async () => {
  await isolated(`
    import assert from 'node:assert/strict';
    import { createRequire } from 'node:module';
    const { NativeRunError } = await import('./src/core/native.ts');
    assert.ok(new NativeRunError('x') instanceof Error);

    // Exercise src/core/native.ts createNativeSession logic directly with synthetic bindings.
    const { readFile } = await import('node:fs/promises');
    const ort = await import('onnxruntime-node');
    const source = (await readFile('./src/core/native.ts', 'utf8'))
      .replace(/^import .*?;\\r?\\n/gm, '')
      .replace(/import\\.meta\\.url/g, "'file:///fake/src/core/native.ts'")
      .replace(/export type [\\s\\S]*?\\n\\};\\r?\\n/, '')
      .replace(/interface NativeBinding [\\s\\S]*?\\n\\}\\r?\\n/, '');
    const ts = await import('typescript');
    const nodePath = await import('node:path');
    const js = ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 } }).outputText;

    function buildWithBinding(binding) {
      const mod = { exports: {} };
      const fn = new Function('exports', 'require', 'module', 'ort', 'existsSync', 'createRequire', 'fileURLToPath', 'path', 'logger', js);
      const fakeRequire = id => {
        if (id === 'onnxruntime-node') return '/fake/node_modules/onnxruntime-node/dist/index.js';
        return binding;
      };
      fakeRequire.resolve = id => '/fake/node_modules/onnxruntime-node/dist/index.js';
      fn(mod.exports, fakeRequire, mod, ort, () => true, () => fakeRequire, u => String(u), nodePath, { warn() {} });
      return mod.exports;
    }

    let runCalls = 0;
    const modFallback = buildWithBinding({
      ssruVersion: 1,
      init: async () => ({}),
      run: async (_h, inp, _cand, eos) => { runCalls++; return BigInt64Array.from([...inp, eos]); },
      close: async () => {},
    });
    const s1 = await modFallback.createNativeSession('m.onnx', ['X', 'eos_id'], 1, true);
    const out1 = await s1.runBatch([BigInt64Array.of(5n), BigInt64Array.of(6n)], [null, null], 2n);
    assert.equal(runCalls, 2);
    assert.deepEqual(out1.map(a => [...a]), [[5n, 2n], [6n, 2n]]);
    await s1.release();
    await assert.rejects(() => s1.runBatch([BigInt64Array.of(5n)], [null], 2n), /closing/);

    let nativeBatchCalls = 0;
    const modNative = buildWithBinding({
      ssruVersion: 1,
      init: async () => ({}),
      run: async () => { throw new Error('should not call single run'); },
      runBatch: async (_h, inputs, cands, eos) => {
        nativeBatchCalls++;
        if (inputs[0][0] === 99n) throw new Error('native batch fail');
        return inputs.map((inp, i) => BigInt64Array.from([BigInt(cands[i]?.length ?? 0), ...inp, eos]));
      },
      close: async () => {},
    });
    const s2 = await modNative.createNativeSession('m.onnx', ['X', 'eos_id', 'candidate_ids'], 1, true);
    const out2 = await s2.runBatch([BigInt64Array.of(7n), BigInt64Array.of(8n)], [BigInt64Array.of(1n, 2n), null], 0n);
    assert.equal(nativeBatchCalls, 1);
    assert.deepEqual(out2.map(a => [...a]), [[2n, 7n, 0n], [0n, 8n, 0n]]);
    await assert.rejects(() => s2.runBatch([BigInt64Array.of(99n)], [null], 0n), e => e instanceof modNative.NativeRunError);
    await s2.release();
  `);
});

test('services/engine throttles statSync, uses touched idle timer, batches long text & controllers', async () => {
  await isolated(`
    import { mock, spyOn } from 'bun:test';
    import assert from 'node:assert/strict';
    import fs from 'node:fs';
    import { mkdir, mkdtemp, writeFile, rm } from 'node:fs/promises';
    import path from 'node:path';
    import { getConfig, setConfig } from './src/config/index.ts';

    await mkdir('.tmp', { recursive: true });
    const root = await mkdtemp(path.resolve('.tmp/service-batch-'));
    await mkdir(path.join(root, 'en_de'));
    await mkdir(path.join(root, 'ja_en'));
    await mkdir(path.join(root, 'en_fr'));
    for (const p of ['en_de', 'ja_en', 'en_fr']) {
      await writeFile(path.join(root, p, 'manifest.json'), JSON.stringify({ identity: 'id-' + p }));
    }
    setConfig({ modelDir: root, workersPerLanguage: 1, workerIdleTimeout: 60, cacheSize: 100, apiToken: '' });

    let timerSeq = 0;
    let clearCalls = 0;
    const timers = new Map();
    spyOn(globalThis, 'setTimeout').mockImplementation((cb, delay) => {
      const id = ++timerSeq;
      timers.set(id, cb);
      return id;
    });
    spyOn(globalThis, 'clearTimeout').mockImplementation(id => { clearCalls++; timers.delete(id); });

    const statOrig = fs.statSync;
    let statCalls = 0;
    spyOn(fs, 'statSync').mockImplementation((...args) => { statCalls++; return statOrig(...args); });

    const batchCalls = [];
    const singleCalls = [];
    mock.module('./src/core/engine.ts', () => ({ TranslationEngine: class {
      busy = false;
      backend = { provider: 'cpu', runtime: 'native-ssru', cpuThreads: 1, tuning: { spinning: false } };
      async init(model) { this.identity = model.identity; this.pair = model.pair; }
      async translateAsync(text) { singleCalls.push([this.pair, text]); return this.pair + ':' + text; }
      async translateBatchAsync(texts) { batchCalls.push([this.pair, [...texts]]); return texts.map(t => this.pair + ':' + t); }
      async destroy() { this.destroyed = true; }
    } }));
    mock.module('./src/models/index.ts', () => ({
      hasLanguagePair: (f, t) => (f === 'en' && (t === 'de' || t === 'fr')) || (f === 'ja' && t === 'en'),
      prepareModel: async (to, from) => ({ identity: 'id-' + from + '_' + to, pair: from + '->' + to }),
    }));
    mock.module('./src/services/detector.ts', () => ({
      detectLanguage: async () => 'en',
      detectMultipleLanguages: async text => [{ language: 'en', text, start: 0, end: text.length }],
    }));

    try {
      const { translateWithPivot, translateBatchWithPivot, cleanupAllEngines } = await import('./src/services/engine.ts');
      const { TranslateController } = await import('./src/controllers/translate.controller.ts');
      const { ImmeController } = await import('./src/controllers/plugins/imme.controller.ts');
      const { GoogleController } = await import('./src/controllers/plugins/google.controller.ts');
      const { KissController } = await import('./src/controllers/plugins/kiss.controller.ts');
      const { DeeplController } = await import('./src/controllers/plugins/deepl.controller.ts');
      const { HcfyController } = await import('./src/controllers/plugins/hcfy.controller.ts');

      // Warm en_de engine and confirm rapid follow-ups do not call statSync or clearTimeout per request.
      assert.equal(await translateWithPivot('en', 'de', 'one'), 'en->de:one');
      const baseStat = statCalls;
      const baseClear = clearCalls;
      assert.equal(await translateWithPivot('en', 'de', 'two'), 'en->de:two');
      assert.equal(await translateWithPivot('en', 'de', 'three'), 'en->de:three');
      assert.equal(statCalls, baseStat);
      assert.equal(clearCalls, baseClear);

      // Idle timer fires once after touch -> keeps engine alive; firing again without touch -> destroys it.
      assert.equal(timers.size, 1);
      const firstCb = [...timers.values()][0];
      timers.clear();
      firstCb();
      // Touched engine re-armed instead of destroyed.
      assert.equal(timers.size, 1);

      // Direct batch & pivot batch go through engine.translateBatchAsync.
      const bat = await translateBatchWithPivot('ja', 'fr', ['s1', 's2']);
      assert.deepEqual(bat, ['en->fr:ja->en:s1', 'en->fr:ja->en:s2']);

      const ctrl = new TranslateController();
      assert.deepEqual((await ctrl.translateBatch({ from: 'en', to: 'de', texts: ['a1', 'a2'] })).results, ['en->de:a1', 'en->de:a2']);
      const imme = await new ImmeController().translate({ source_lang: 'en', target_lang: 'de', text_list: ['i1', '', 'i2'] });
      assert.deepEqual(imme.translations.map(x => x.text), ['en->de:i1', '', 'en->de:i2']);
      const google = await new GoogleController().translateCompat({ q: ['g1', 'g2'], source: 'en', target: 'de' });
      assert.deepEqual(google.data.translations.map(x => x.translatedText), ['en->de:g1', 'en->de:g2']);
      const kiss = await new KissController().translate({ from: 'en', to: 'de', texts: ['k1', 'k2'] });
      assert.deepEqual(kiss.translations?.map(x => x.text), ['en->de:k1', 'en->de:k2']);
      const deepl = await new DeeplController().translate({ target_lang: 'de', source_lang: 'en', text: ['d1', 'd2'] });
      assert.deepEqual(deepl.translations.map(x => x.text), ['en->de:d1', 'en->de:d2']);
      const hcfy = await new HcfyController().translate({ text: 'h1\\n\\nh2', source: '英语', destination: ['de'] });
      assert.deepEqual(hcfy.result, ['en->de:h1', '', 'en->de:h2']);

      // Long text with auto detection splits and runs translateBatchAsync once instead of serially.
      setConfig({ maxSentenceLength: 20 });
      const beforeBatches = batchCalls.length;
      const longOut = await translateWithPivot('auto', 'de', 'First sentence. Second sentence. Third sentence.');
      assert.equal(longOut, 'en->de:First sentence. en->de:Second sentence. en->de:Third sentence.');
      assert.equal(batchCalls.length, beforeBatches + 1);

      await cleanupAllEngines();
    } finally {
      await rm(root, { recursive: true, force: true });
    }
  `);
});

test('HTTP direct /translate & /translate/batch routes preserve auth, strict validation, headers and Swagger', async () => {
  await isolated(`
    import { mock } from 'bun:test';
    import assert from 'node:assert/strict';
    import { createServer } from 'node:net';
    import { mkdir, mkdtemp, rm } from 'node:fs/promises';
    import path from 'node:path';
    import { setConfig } from './src/config/index.ts';

    await mkdir('.tmp', { recursive: true });
    const root = await mkdtemp(path.resolve('.tmp/http-fast-'));
    const freePort = await new Promise((resolve, reject) => {
      const s = createServer();
      s.listen(0, '127.0.0.1', () => {
        const addr = s.address();
        s.close(() => resolve(typeof addr === 'object' && addr ? addr.port : 0));
      });
      s.on('error', reject);
    });
    setConfig({
      host: '127.0.0.1',
      port: String(freePort),
      modelDir: root,
      configDir: root,
      logConsole: false,
      logToFile: false,
      checkUpdate: false,
      apiToken: 'secret-tok',
    });

    mock.module('./src/models/index.ts', () => ({ initRecords: async () => {} }));
    mock.module('./src/assets/index.ts', () => ({ cleanupLegacyBin: async () => {} }));
    mock.module('./src/middleware/ui.ts', () => ({ uiStatic: (_req, _res, next) => next() }));
    mock.module('./src/middleware/swagger.ts', () => ({ swaggerStatic: (_req, _res, next) => next() }));
    mock.module('./src/services/index.ts', () => ({
      warmupEngines: async () => {},
      cleanupAllEngines: async () => {},
      getEngineRuntimes: () => [],
      translateWithPivot: async (f, t, text, html) => f + '->' + t + ':' + text + ':' + html,
      translateBatchWithPivot: async (f, t, texts, html) => texts.map(s => f + '->' + t + ':' + s + ':' + html),
    }));

    const { startServer } = await import('./src/server/index.ts');
    const server = await startServer({ handleSignals: false });
    const base = 'http://127.0.0.1:' + freePort;
    try {
      // Unauthorized without token
      const r401 = await fetch(base + '/translate', {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ from: 'en', to: 'zh-CN', text: 'hi' }),
      });
      assert.equal(r401.status, 401);
      assert.ok(r401.headers.get('x-request-id'));
      assert.equal(r401.headers.get('access-control-allow-origin'), '*');

      // Valid auth via Bearer and x-api-token
      const rOk = await fetch(base + '/translate', {
        method: 'POST',
        headers: { 'content-type': 'application/json', authorization: 'Bearer secret-tok', 'x-request-id': 'req-42' },
        body: JSON.stringify({ from: 'en', to: 'zh-CN', text: 'hello', html: 'true' }),
      });
      assert.equal(rOk.status, 200);
      assert.equal(rOk.headers.get('x-request-id'), 'req-42');
      assert.deepEqual(await rOk.json(), { result: 'en->zh-Hans:hello:true' });

      const rBatch = await fetch(base + '/translate/batch?token=secret-tok', {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ from: 'en', to: 'ja', texts: ['a', 'b'] }),
      });
      assert.equal(rBatch.status, 200);
      assert.deepEqual(await rBatch.json(), { results: ['en->ja:a:false', 'en->ja:b:false'] });

      // Extra unknown property rejected identically to tsoa
      const rExtra = await fetch(base + '/translate', {
        method: 'POST',
        headers: { 'content-type': 'application/json', 'x-api-token': 'secret-tok' },
        body: JSON.stringify({ from: 'en', to: 'ja', text: 'a', extra: 1 }),
      });
      assert.equal(rExtra.status, 500);

      // OPTIONS preflight and tsoa endpoints remain operational
      const rOpt = await fetch(base + '/translate', { method: 'OPTIONS' });
      assert.equal(rOpt.status, 204);
      const rVer = await fetch(base + '/version');
      assert.equal(rVer.status, 200);
    } finally {
      await server.stop();
      await rm(root, { recursive: true, force: true });
    }
  `);
});
