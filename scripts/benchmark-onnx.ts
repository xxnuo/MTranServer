// Linux: bun scripts/benchmark-onnx.ts dist/main.js /path/to/prepared/models output.json
import assert from 'node:assert/strict';
import { constants, readFileSync } from 'node:fs';
import { cp, mkdtemp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { spawn, execFileSync } from 'node:child_process';
import { once } from 'node:events';
import { createHash } from 'node:crypto';
import { createServer } from 'node:net';
import os from 'node:os';
import path from 'node:path';

const [entryArg, modelsArg, outputArg] = process.argv.slice(2);
assert(entryArg && modelsArg && outputArg, 'Usage: benchmark-onnx server-entry.js prepared-models output.json');
const entry = path.resolve(entryArg), modelRoot = path.resolve(modelsArg), output = path.resolve(outputArg);
const corpusFile = new URL('../benchmarks/onnx-corpus.json', import.meta.url);
const corpus: Record<string, string>[] = JSON.parse(await readFile(corpusFile, 'utf8'));
const pairs = [['en', 'zh-Hans'], ['zh-Hans', 'en'], ['de', 'en']];
const rounds = 3, warmupRequests = 8;
const root = await mkdtemp(path.join(os.tmpdir(), 'mtran-onnx-benchmark-'));
const sha256 = (data: string | Buffer) => createHash('sha256').update(data).digest('hex');
const git = (...args: string[]) => execFileSync('git', args, { encoding: 'utf8' }).trim();
const sleep = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));
const results: any[] = [];
const report = {
  startedAt: new Date().toISOString(), finishedAt: '',
  environment: {
    cpu: os.cpus()[0].model, logicalCpus: os.cpus().length, totalMemoryBytes: os.totalmem(),
    platform: os.platform(), architecture: os.arch(), kernel: os.release(),
    osRelease: readFileSync('/etc/os-release', 'utf8'),
    nodeVersion: execFileSync('node', ['--version'], { encoding: 'utf8' }).trim(),
    onnxruntimeVersion: JSON.parse(readFileSync(new URL('../node_modules/onnxruntime-node/package.json', import.meta.url), 'utf8')).version,
    loadAverageAtStart: os.loadavg(), loadAverageAtEnd: [] as number[],
    gitCommit: git('rev-parse', 'HEAD'), workingTreeChanges: git('diff', '--name-only').split('\n'),
    serverEntrySha256: sha256(await readFile(entry)),
  },
  method: {
    endpoint: 'POST /translate on loopback HTTP', executionProvider: 'automatic; see each result.runtime',
    sessionTuning: 'automatic; see each result.runtime.tuning and maxConcurrentRuns', cacheSize: 0, rounds, warmupRequests,
    corpus: 'onnx-corpus.json', corpusSha256: sha256(await readFile(corpusFile)),
    corpusDescription: '32 fixed, self-authored everyday/software sentences per language; 8 paragraphs formed by joining consecutive groups of 4 sentences.',
    percentile: 'nearest rank, ceil(p * sample count) - 1',
    coldRequest: 'Fresh server process, including automatic backend selection; prepared models, no download/conversion or OS page-cache eviction.',
    memory: 'Linux VmHWM for the entire server process (including model loading); one fresh process per language pair.',
    backgroundLoad: 'Shared desktop machine; other applications and compute tasks were left running.',
  },
  results,
};
try {
  const records: unknown[] = [];
  for (const [from, to] of pairs) {
    const pair = `${from}_${to}`;
    await cp(path.join(modelRoot, pair), path.join(root, 'models', pair), { recursive: true, mode: constants.COPYFILE_FICLONE });
    records.push(...JSON.parse(await readFile(path.join(root, 'models', pair, 'manifest.json'), 'utf8')).records);
  }
  await mkdir(path.join(root, 'config'));
  await writeFile(path.join(root, 'config', 'records.json'), JSON.stringify({ data: records }));
  for (const [from, to] of pairs) {
    const pair = `${from}_${to}`;
    const preparedModelSha256 = sha256(await readFile(path.join(root, 'models', pair, 'model.onnx')));
    const reservation = createServer();
    reservation.listen(0, '127.0.0.1');
    await once(reservation, 'listening');
    const port = (reservation.address() as { port: number }).port;
    await new Promise<void>((resolve, reject) => reservation.close(error => error ? reject(error) : resolve()));
    const url = `http://127.0.0.1:${port}`;
    const child = spawn('node', [entry, '--host', '127.0.0.1', '--port', String(port),
      '--model-dir', path.join(root, 'models'), '--config-dir', path.join(root, 'config'),
      '--log-dir', path.join(root, 'logs'), '--offline', '--cache-size', '0',
      '--worker-idle-timeout', '600', '--max-sentence-length', '512', '--api-token', '',
      '--fullwidth-zh-punctuation', '--no-check-update', '--no-log-console'], { stdio: ['ignore', 'pipe', 'pipe'] });
    let log = '';
    child.stdout.on('data', data => { log += data; });
    child.stderr.on('data', data => { log += data; });
    child.on('error', error => { log += error.message; });
    const memory = () => {
      const status = readFileSync(`/proc/${child.pid}/status`, 'utf8');
      return {
        rssMiB: Number(/VmRSS:\s+(\d+)/.exec(status)![1]) / 1024,
        peakRssMiB: Number(/VmHWM:\s+(\d+)/.exec(status)![1]) / 1024,
      };
    };
    try {
      let ready = false;
      for (let attempt = 0; attempt < 300; attempt++) {
        assert(child.exitCode === null, `Server exited: ${log}`);
        try { ready = (await fetch(url + '/health', { signal: AbortSignal.timeout(1000) })).ok; } catch {}
        if (ready) break;
        await sleep(100);
      }
      assert(ready, `Server did not start: ${log}`);
      const settingsResponse = await fetch(url + '/ui/api/settings');
      assert(settingsResponse.ok, 'Cannot verify server settings');
      const settings = await settingsResponse.json() as any;
      assert.equal(settings.config?.server?.cacheSize, 0, 'Benchmark requires MT_CACHE_SIZE=0');
      const request = async (text: string) => {
        const started = performance.now();
        const response = await fetch(url + '/translate', {
          method: 'POST', headers: { 'content-type': 'application/json' },
          body: JSON.stringify({ from, to, text }), signal: AbortSignal.timeout(120000),
        });
        if (!response.ok) assert.fail(`${pair}: HTTP ${response.status}: ${await response.text()}`);
        const data = await response.json() as { result: string };
        assert(typeof data.result === 'string' && data.result.trim() && data.result !== text, `Missing translation for ${pair}`);
        return { elapsedMs: performance.now() - started, translation: data.result };
      };
      const sentences = corpus.map(row => row[from]);
      assert(sentences.length === 32 && new Set(sentences).size === 32 && sentences.every(Boolean), 'Expected 32 distinct sentences');
      const paragraphs = Array.from({ length: 8 }, (_, i) => sentences.slice(i * 4, i * 4 + 4).join(' '));
      const result = { pair, serverVersion: settings.version, serverSettings: settings.config.server, runtime: {} as any,
        baselineMemory: memory(), coldRequest: await request(sentences[0]), warmMemory: {}, model: {}, cases: [] as any[] };
      for (const text of sentences.slice(0, warmupRequests)) await request(text);
      result.warmMemory = memory();
      const runtimeSettings = await (await fetch(url + '/ui/api/settings')).json() as any;
      result.runtime = runtimeSettings.engines?.find((engine: any) => engine.from === from && engine.to === to);
      assert(result.runtime?.provider && result.runtime?.cpuThreads >= 0, 'Cannot verify the selected inference backend');
      const manifest = JSON.parse(await readFile(path.join(root, 'models', pair, 'manifest.json'), 'utf8'));
      const model = await readFile(path.join(root, 'models', pair, 'model.onnx'));
      assert.equal(sha256(model), preparedModelSha256, 'Model was converted during timing; prepare compatible models before benchmarking');
      result.model = { bytes: model.length, sha256: sha256(model), identity: manifest.identity, converterRevision: manifest.converterRevision,
        records: manifest.records.map((r: any) => ({ fileType: r.fileType, version: r.version, decompressedHash: r.decompressedHash })) };
      for (const [name, concurrency, texts] of [['sentence', 1, sentences], ['sentence', 4, sentences], ['paragraph', 1, paragraphs]] as const) {
        const measurements: any[] = [];
        const expected: string[] = [];
        for (let round = 0; round < rounds; round++) {
          let next = 0;
          const samples: any[] = new Array(texts.length);
          const started = performance.now();
          await Promise.all(Array.from({ length: concurrency }, async () => {
            while (next < texts.length) {
              const index = next++;
              const sample = await request(texts[index]);
              if (round === 0) expected[index] = sample.translation;
              else assert.equal(sample.translation, expected[index], `Non-deterministic output: ${pair}/${name}/${index}`);
              samples[index] = { index, ...sample };
            }
          }));
          const elapsedMs = performance.now() - started;
          measurements.push({ round: round + 1, elapsedMs, requestsPerSecond: texts.length * 1000 / elapsedMs,
            loadAverage: os.loadavg(), memory: memory(), samples });
          console.log(`${pair} ${name} concurrency=${concurrency} round=${round + 1}: ${(texts.length * 1000 / elapsedMs).toFixed(2)} requests/s`);
        }
        const times = measurements.flatMap(r => r.samples.map((s: any) => s.elapsedMs)).sort((a, b) => a - b);
        const rates = measurements.map(r => r.requestsPerSecond).sort((a, b) => a - b);
        const lengths = texts.map(text => Array.from(text).length);
        result.cases.push({ name, concurrency, uniqueTexts: texts.length, requests: times.length,
          inputCharacters: { min: Math.min(...lengths), mean: lengths.reduce((a, b) => a + b, 0) / lengths.length, max: Math.max(...lengths) },
          summary: { meanMs: times.reduce((a, b) => a + b, 0) / times.length,
            p50Ms: times[Math.ceil(times.length * .50) - 1], p95Ms: times[Math.ceil(times.length * .95) - 1],
            medianRequestsPerSecond: rates[1], minRequestsPerSecond: rates[0], maxRequestsPerSecond: rates[2],
            peakRssMiB: memory().peakRssMiB, errors: 0 }, measurements });
      }
      results.push(result);
      const finalSettings = await (await fetch(url + '/ui/api/settings')).json() as any;
      assert.deepEqual(finalSettings.engines?.find((engine: any) => engine.from === from && engine.to === to), result.runtime,
        'Inference backend changed during benchmarking; rerun after fallback');
      await mkdir(path.dirname(output), { recursive: true });
      await writeFile(output, JSON.stringify(report, null, 2) + '\n');
    } finally {
      if (child.pid && child.exitCode === null && child.signalCode === null) {
        const exited = once(child, 'exit');
        child.kill('SIGTERM');
        const timer = setTimeout(() => child.kill('SIGKILL'), 5000);
        try { await exited; } finally { clearTimeout(timer); }
      }
    }
  }
  report.finishedAt = new Date().toISOString();
  report.environment.loadAverageAtEnd = os.loadavg();
  await writeFile(output, JSON.stringify(report, null, 2) + '\n');
  console.log(`Results: ${output}`);
} finally {
  await rm(root, { recursive: true, force: true });
}
