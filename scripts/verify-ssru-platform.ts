import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { createRequire } from 'node:module';
import { execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { cpus, release } from 'node:os';
import path from 'node:path';
import { decompress } from 'fzstd';
import { readRelease } from '../src/converter/release';
import { buildModel } from '../src/converter/graph';
import { Tokenizer } from '../src/core/tokenizer';
import { Shortlist } from '../src/core/shortlist';
import { nativeInputHashes } from './native-inputs';

// Run the bundled script with Node on the target machine, never an emulator.
const root = process.cwd();
const work = path.resolve(process.env.SSRU_VERIFY_DIR ?? '.tmp/ssru-platform');
const require = createRequire(import.meta.url);
const addonPath = path.join(root, `dist/onnx-async-${process.platform}-${process.arch}.node`);
const addon = require(addonPath);
assert.equal(addon.ssruVersion, 1, 'Native SSRU addon is required; fallback is not a passing result');
const library = path.resolve(path.dirname(require.resolve('onnxruntime-node')), '../bin/napi-v6', process.platform, process.arch,
  process.platform === 'win32' ? 'onnxruntime.dll' : process.platform === 'darwin' ? 'libonnxruntime.1.dylib' : 'libonnxruntime.so.1');
const fixtures = JSON.parse(await readFile('scripts/ssru-platform-models.json', 'utf8'));
const corpus = JSON.parse(await readFile('docs/benchmarks/fast-corpus.json', 'utf8'));
const hash = (bytes: Uint8Array) => createHash('sha256').update(bytes).digest('hex');
await mkdir(work, { recursive: true });
const flores = process.env.SSRU_VERIFY_FLORES === '1';
if (flores) {
  const filename = 'flores200_dataset.tar.gz';
  const destination = path.join(work, filename);
  let bytes = await readFile(destination).catch(() => undefined);
  if (!bytes) {
    const response = await fetch('https://dl.fbaipublicfiles.com/nllb/flores200_dataset.tar.gz');
    assert(response.ok, 'Cannot download FLORES-200 validation corpus');
    bytes = Buffer.from(await response.arrayBuffer());
    await writeFile(destination, bytes);
  }
  assert.equal(hash(bytes), 'b8b0b76783024b85797e5cc75064eb83fc5288b41e9654dabc7be6ae944011f6', 'FLORES archive hash mismatch');
  execFileSync('tar', ['-xzf', filename, './flores200_dataset/devtest/eng_Latn.devtest', './flores200_dataset/devtest/zho_Hans.devtest'], { cwd: work });
}
const results: unknown[] = [];
let mismatches = 0;
for (const fixture of fixtures) {
  const directory = path.join(work, fixture.pair);
  await mkdir(directory, { recursive: true });
  for (const file of fixture.files) {
    const filename = path.join(directory, file.path);
    const existing = await readFile(filename).catch(() => undefined);
    if (existing && hash(existing) === file.sha256) continue;
    const response = await fetch(file.url);
    assert(response.ok, `Download failed: ${file.url}: ${response.status}`);
    const compressed = new Uint8Array(await response.arrayBuffer());
    assert.equal(hash(compressed), file.downloadSha256, 'Compressed source hash mismatch');
    const bytes = file.url.endsWith('.zst') ? decompress(compressed) : compressed;
    assert.equal(hash(bytes), file.sha256, 'Source hash mismatch');
    await writeFile(filename, bytes);
  }
  const file = (role: string) => {
    const source = fixture.files.find((f: { role: string }) => f.role === role)
      ?? (role.includes('vocab') ? fixture.files.find((f: { role: string }) => f.role === 'source-vocab') : undefined);
    assert(source, `Missing fixture role: ${role}`);
    return path.join(directory, source.path);
  };
  const { weights, config } = await readRelease(file('source-model'));
  const baselinePath = path.join(directory, 'fast.onnx');
  const candidatePath = path.join(directory, 'ssru.onnx');
  await writeFile(baselinePath, buildModel(weights, config, 'qall', true));
  await writeFile(candidatePath, buildModel(weights, config, 'qall', true, true));
  const source = await Tokenizer.load(file('source-srcvocab'));
  const target = await Tokenizer.load(file('source-trgvocab'));
  const shortlist = await Shortlist.load(file('source-lex'), file('source-srcvocab'), file('source-trgvocab'));
  const baseline = await addon.init(baselinePath, library, 1);
  const candidate = await addon.init(candidatePath, library, 1);
  try {
    const rows = corpus.filter((row: { from: string }) => fixture.pair.startsWith(row.from === 'en' ? 'en_' : 'zh-Hans_'));
    const prepared = rows.map((row: { text: string }) => {
      const ids = [...source.encode(row.text), source.eos];
      return { text: row.text, x: BigInt64Array.from(ids, BigInt), c: BigInt64Array.from(shortlist.candidates(ids), BigInt) };
    });
    const run = (handle: object, item: typeof prepared[number]) => addon.run(handle, item.x, item.c, BigInt(target.eos)) as Promise<BigInt64Array>;
    const expected: BigInt64Array[] = [];
    const actual: BigInt64Array[] = [];
    const differences: unknown[] = [];
    for (const item of prepared) {
      const a = await run(baseline, item), b = await run(candidate, item);
      assert(a.includes(BigInt(target.eos)) && b.includes(BigInt(target.eos)), 'Generation limit is not a successful translation');
      if (a.length !== b.length || a.some((token, i) => token !== b[i])) {
        mismatches++;
        differences.push({ source: item.text, baseline: Array.from(a, Number), candidate: Array.from(b, Number),
          baseline_text: target.decode(Array.from(a, Number).filter(id => id !== target.eos)),
          candidate_text: target.decode(Array.from(b, Number).filter(id => id !== target.eos)) });
      }
      expected.push(a);
      actual.push(b);
    }
    const measurements: unknown[] = [];
    for (let round = 0; round < 3; round++) {
      for (const name of round % 2 ? ['candidate', 'fast'] : ['fast', 'candidate']) {
        const handle = name === 'candidate' ? candidate : baseline;
        for (const concurrency of [1, 8]) {
          let next = 0;
          const latencies: number[] = [];
          const count = prepared.length * 3, start = performance.now();
          await Promise.all(Array.from({ length: concurrency }, async () => {
            while (next < count) {
              const index = next++ % prepared.length;
              const started = performance.now();
              assert.deepEqual(await run(handle, prepared[index]), name === 'candidate' ? actual[index] : expected[index], 'Non-deterministic concurrent output');
              latencies.push(performance.now() - started);
            }
          }));
          const ms = performance.now() - start;
          const sorted = [...latencies].sort((a, b) => a - b), percentile = .95 * (sorted.length - 1);
          const p95 = sorted[Math.floor(percentile)] + (sorted[Math.ceil(percentile)] - sorted[Math.floor(percentile)]) * (percentile % 1);
          measurements.push({ round, name, concurrency, requests: count, wall_ms: ms, rps: count * 1000 / ms,
            latencies_ms: latencies, p95_ms: p95, memory: process.memoryUsage() });
        }
      }
    }
    let qualitySamples = 0;
    if (flores) {
      const language = fixture.pair.startsWith('en_') ? 'eng_Latn' : 'zho_Hans';
      const lines = (await readFile(path.join(work, `flores200_dataset/devtest/${language}.devtest`), 'utf8')).trim().split('\n');
      assert.equal(lines.length, 1012, 'Unexpected FLORES devtest size');
      for (const text of lines) {
        const ids = [...source.encode(text), source.eos];
        const item = { text, x: BigInt64Array.from(ids, BigInt), c: BigInt64Array.from(shortlist.candidates(ids), BigInt) };
        const a = await run(baseline, item), b = await run(candidate, item);
        assert(a.includes(BigInt(target.eos)) && b.includes(BigInt(target.eos)), 'FLORES generation limit');
        if (a.length !== b.length || a.some((token, i) => token !== b[i])) {
          mismatches++;
          differences.push({ corpus: 'FLORES-200 devtest', index: qualitySamples, source: text,
            baseline_text: target.decode(Array.from(a, Number).filter(id => id !== target.eos)),
            candidate_text: target.decode(Array.from(b, Number).filter(id => id !== target.eos)) });
        }
        qualitySamples++;
      }
    }
    results.push({ pair: fixture.pair, samples: expected.length + qualitySamples, token_exact: expected.length + qualitySamples - differences.length,
      flores_devtest_samples: qualitySamples,
      differences, model_sha256: hash(await readFile(file('source-model'))), measurements });
  } finally {
    await addon.close(candidate); await addon.close(baseline);
    source.close(); target.close();
  }
}
await writeFile(path.join(work, 'result.json'), JSON.stringify({
  platform: process.platform, arch: process.arch, os_release: release(), cpu: cpus()[0]?.model,
  node: process.version, ort: require('onnxruntime-node/package.json').version,
  preferred_backend: addon.ssruBackend, addon_sha256: hash(await readFile(addonPath)),
  source_sha256: nativeInputHashes(root),
  finished_at: new Date().toISOString(), memory: process.memoryUsage(), results,
}, null, 2) + '\n');
assert.equal(mismatches, 0, 'Token differences require independent quality review; inspect result.json');
console.log(`PASS ${process.platform}/${process.arch}: ${path.join(work, 'result.json')}`);
