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
interface PairSummary {
  pair: string;
  mode: string;
  corpus_exact_rate: number;
  corpus_len_ratio: number;
  flores_devtest_samples: number;
  flores_exact_rate: number | null;
  flores_len_ratio: number | null;
  [key: string]: unknown;
}
const results: PairSummary[] = [];
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
  const modelSha256 = hash(await readFile(file('source-model')));
  const source = await Tokenizer.load(file('source-srcvocab'));
  const target = await Tokenizer.load(file('source-trgvocab'));
  const shortlist = await Shortlist.load(file('source-lex'), file('source-srcvocab'), file('source-trgvocab'));
  try {
    const rows = corpus.filter((row: { from: string }) => fixture.pair.startsWith(row.from === 'en' ? 'en_' : 'zh-Hans_'));
    const language = fixture.pair.startsWith('en_') ? 'eng_Latn' : 'zho_Hans';
    const floresLines = flores
      ? (await readFile(path.join(work, `flores200_dataset/devtest/${language}.devtest`), 'utf8')).trim().split('\n')
      : [];
    if (flores) assert.equal(floresLines.length, 1012, 'Unexpected FLORES devtest size');
    for (const useShortlist of [true, false]) {
      const mode = useShortlist ? 'fast' : 'standard';
      const baselinePath = path.join(directory, useShortlist ? 'fast.onnx' : 'standard.onnx');
      const candidatePath = path.join(directory, useShortlist ? 'ssru.onnx' : 'ssru-standard.onnx');
      await writeFile(baselinePath, buildModel(weights, config, 'qall', useShortlist));
      await writeFile(candidatePath, buildModel(weights, config, 'qall', useShortlist, true));
      const baseline = await addon.init(baselinePath, library, 1);
      const candidate = await addon.init(candidatePath, library, 1);
      try {
        const encodeItem = (text: string) => {
          const ids = [...source.encode(text), source.eos];
          return {
            text,
            x: BigInt64Array.from(ids, BigInt),
            c: useShortlist ? BigInt64Array.from(shortlist.candidates(ids), BigInt) : null,
          };
        };
        const prepared = rows.map((row: { text: string }) => encodeItem(row.text));
        const run = (handle: object, item: ReturnType<typeof encodeItem>) =>
          addon.run(handle, item.x, item.c, BigInt(target.eos)) as Promise<BigInt64Array>;
        const expected: BigInt64Array[] = [];
        const actual: BigInt64Array[] = [];
        const decodeText = (tokens: BigInt64Array) =>
          target.decode(Array.from(tokens, Number).filter(id => id !== target.eos));
        let pairCorpusMismatches = 0;
        let corpusBaseChars = 0;
        let corpusCandChars = 0;
        const differences: unknown[] = [];
        for (const [idx, item] of prepared.entries()) {
          const a = await run(baseline, item), b = await run(candidate, item);
          assert(a.includes(BigInt(target.eos)) && b.includes(BigInt(target.eos)),
            `Generation limit is not a successful translation (${fixture.pair} ${mode} corpus #${idx}: base_eos=${a.includes(BigInt(target.eos))} base_len=${a.length} cand_eos=${b.includes(BigInt(target.eos))} cand_len=${b.length})`);
          const baseText = decodeText(a);
          const candText = decodeText(b);
          assert(candText.trim().length > 0, 'Empty corpus candidate translation');
          corpusBaseChars += baseText.length;
          corpusCandChars += candText.length;
          if (a.length !== b.length || a.some((token, i) => token !== b[i])) {
            pairCorpusMismatches++;
            differences.push({ mode, source: item.text, baseline: Array.from(a, Number), candidate: Array.from(b, Number),
              baseline_text: baseText,
              candidate_text: candText });
          }
          expected.push(a);
          actual.push(b);
        }
        const measurements: unknown[] = [];
        const baseLabel = useShortlist ? 'fast' : 'standard';
        for (let round = 0; round < 3; round++) {
          for (const name of round % 2 ? ['candidate', baseLabel] : [baseLabel, 'candidate']) {
            const handle = name === 'candidate' ? candidate : baseline;
            for (const concurrency of [1, 8]) {
              let next = 0;
              const latencies: number[] = [];
              const count = prepared.length * (useShortlist ? 3 : 1), start = performance.now();
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
              measurements.push({ round, mode, name, concurrency, requests: count, wall_ms: ms, rps: count * 1000 / ms,
                latencies_ms: latencies, p95_ms: p95, memory: process.memoryUsage() });
            }
          }
        }
        let qualitySamples = 0;
        let floresMismatches = 0;
        let baseChars = 0;
        let candChars = 0;
        if (flores) {
          const floresItems = floresLines.map(encodeItem);
          const baseOutputs = new Array<BigInt64Array>(floresItems.length);
          const candOutputs = new Array<BigInt64Array>(floresItems.length);
          let nextBase = 0;
          await Promise.all(Array.from({ length: 8 }, async () => {
            while (nextBase < floresItems.length) {
              const idx = nextBase++;
              baseOutputs[idx] = await run(baseline, floresItems[idx]);
            }
          }));
          for (let idx = 0; idx < Math.min(128, floresItems.length); idx++) {
            candOutputs[idx] = await run(candidate, floresItems[idx]);
          }
          let nextCand = 128;
          await Promise.all(Array.from({ length: 8 }, async () => {
            while (nextCand < floresItems.length) {
              const idx = nextCand++;
              candOutputs[idx] = await run(candidate, floresItems[idx]);
            }
          }));
          for (let idx = 0; idx < floresItems.length; idx++) {
            const a = baseOutputs[idx], b = candOutputs[idx];
            assert(a.includes(BigInt(target.eos)) && b.includes(BigInt(target.eos)),
              `FLORES generation limit (${fixture.pair} ${mode} #${idx}: base_eos=${a.includes(BigInt(target.eos))} base_len=${a.length} cand_eos=${b.includes(BigInt(target.eos))} cand_len=${b.length})`);
            const baseText = decodeText(a);
            const candText = decodeText(b);
            assert(candText.trim().length > 0, 'Empty FLORES candidate translation');
            baseChars += baseText.length;
            candChars += candText.length;
            if (a.length !== b.length || a.some((token, i) => token !== b[i])) {
              floresMismatches++;
              differences.push({ mode, corpus: 'FLORES-200 devtest', index: idx, source: floresItems[idx].text,
                baseline_text: baseText,
                candidate_text: candText });
            }
            qualitySamples++;
          }
        }
        const corpusExactRate = prepared.length > 0 ? (prepared.length - pairCorpusMismatches) / prepared.length : 1;
        const corpusLenRatio = corpusBaseChars > 0 ? corpusCandChars / corpusBaseChars : 1;
        const floresExactRate = qualitySamples > 0 ? (qualitySamples - floresMismatches) / qualitySamples : null;
        const floresLenRatio = qualitySamples > 0 && baseChars > 0 ? candChars / baseChars : null;
        results.push({ pair: fixture.pair, mode, samples: expected.length + qualitySamples,
          token_exact: expected.length + qualitySamples - differences.length,
          flores_devtest_samples: qualitySamples,
          corpus_mismatches: pairCorpusMismatches,
          corpus_exact_rate: corpusExactRate,
          corpus_len_ratio: corpusLenRatio,
          flores_exact_rate: floresExactRate,
          flores_len_ratio: floresLenRatio,
          differences, model_sha256: modelSha256, measurements });
      } finally {
        await addon.close(candidate); await addon.close(baseline);
      }
    }
  } finally {
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
for (const row of results) {
  const floresInfo = row.flores_devtest_samples > 0 && row.flores_exact_rate !== null && row.flores_len_ratio !== null
    ? `, flores=${(row.flores_exact_rate * 100).toFixed(2)}% (len=${row.flores_len_ratio.toFixed(4)})`
    : '';
  console.log(`${row.pair} ${row.mode} [${addon.ssruBackend ?? 'ort'}]: corpus=${(row.corpus_exact_rate * 100).toFixed(2)}% (len=${row.corpus_len_ratio.toFixed(4)})${floresInfo}`);
}
for (const row of results) {
  assert(row.corpus_exact_rate >= 0.90, `Corpus token-exact rate below 90% for ${row.pair} ${row.mode}: ${(row.corpus_exact_rate * 100).toFixed(2)}%`);
  assert(row.corpus_len_ratio >= 0.95 && row.corpus_len_ratio <= 1.05, `Corpus length ratio out of bounds for ${row.pair} ${row.mode}: ${row.corpus_len_ratio.toFixed(4)}`);
  if (row.flores_devtest_samples > 0) {
    assert(row.flores_exact_rate !== null && row.flores_exact_rate >= 0.90, `FLORES token-exact rate below 90% for ${row.pair} ${row.mode}: ${((row.flores_exact_rate ?? 0) * 100).toFixed(2)}%`);
    assert(row.flores_len_ratio !== null && row.flores_len_ratio >= 0.95 && row.flores_len_ratio <= 1.05, `FLORES length ratio out of bounds for ${row.pair} ${row.mode}: ${(row.flores_len_ratio ?? 0).toFixed(4)}`);
  }
}
console.log(`PASS ${process.platform}/${process.arch}: ${path.join(work, 'result.json')}`);
