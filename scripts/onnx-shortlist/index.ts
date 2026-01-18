import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile, writeFile, mkdir, realpath, lstat, appendFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { parseArgs } from 'node:util';
import * as ort from 'onnxruntime-node';
import { Tokenizer } from '../../src/core/tokenizer.ts';
import { parseLexicon, generateCandidates, validateRequest, byteTokenIds } from './shortlist.ts';
import { modelType, rewriteProjection } from './variant.ts';

const project = fileURLToPath(new URL('../../', import.meta.url));
const hash = (bytes: Uint8Array | string) => createHash('sha256').update(bytes).digest('hex');
const help = `实验性 ONNX shortlist 基准：会改变译文，不修改服务默认行为。
用法：node --import tsx scripts/onnx-shortlist/index.ts
  --model-dir <含 manifest.json 的模型目录>
  --texts '<JSON 文本数组，例如 ["Hello","Good morning"]>'
  --output-dir <项目 .tmp 下尚不存在的目录>
  [--threads <正整数，默认 1>] [--rounds <正整数，默认 3>]
保留原模型；输出 candidate-fp32.onnx、results.jsonl 和 result.json。
同进程 CPU 双 session，全语料交替 warmup 后交替计时。
totalMs 包含 tokenization、候选生成、tensor 构造、推理及解码；
模型生成和 session 创建另行记录。baseline 无 EOS 的样本不计为成功。`;

// Reject existing directories and symlink components before writing experimental artifacts.
export async function reserveOutput(directory: string) {
  const tmp = path.resolve(project, '.tmp'), output = path.resolve(directory);
  const relative = path.relative(tmp, output);
  assert(relative && relative !== '..' && !relative.startsWith('../') && !path.isAbsolute(relative), 'output-dir must be inside project .tmp');
  await mkdir(tmp, { recursive: true });
  assert.equal(await realpath(tmp), tmp, 'project .tmp must not be a symlink');
  let current = tmp;
  const parts = relative.split(path.sep);
  for (let i = 0; i < parts.length; i++) {
    current = path.join(current, parts[i]);
    if (i === parts.length - 1) { await mkdir(current); break; }
    try { await mkdir(current); } catch (error: any) { if (error.code !== 'EEXIST') throw error; }
    const stat = await lstat(current);
    assert(stat.isDirectory() && !stat.isSymbolicLink(), 'output path must not contain symlinks');
  }
  assert.equal(await realpath(output), output, 'output path must not contain symlinks');
  return output;
}

async function main() {
  const { values } = parseArgs({ options: {
    'model-dir': { type: 'string' }, texts: { type: 'string' }, 'output-dir': { type: 'string' },
    threads: { type: 'string', default: '1' }, rounds: { type: 'string', default: '3' }, help: { type: 'boolean' },
  } });
  if (values.help) { console.log(help); return; }
  assert(values['model-dir'] && values.texts && values['output-dir'], help);
  const threads = Number(values.threads), rounds = Number(values.rounds);
  assert(Number.isSafeInteger(threads) && threads > 0 && Number.isSafeInteger(rounds) && rounds > 0, 'threads and rounds must be positive integers');
  const texts: unknown = JSON.parse(values.texts);
  assert(Array.isArray(texts) && texts.length > 0 && texts.every(t => typeof t === 'string'), 'texts must be a nonempty JSON string array');
  const modelDir = await realpath(values['model-dir']);
  const manifestBytes = await readFile(path.join(modelDir, 'manifest.json'));
  const manifest = JSON.parse(manifestBytes.toString());
  assert.equal(manifest.schemaVersion, 1, 'unsupported manifest');
  assert(Array.isArray(manifest.files), 'missing manifest files');
  const file = async (...roles: string[]) => {
    const entries = manifest.files.filter((f: any) => roles.includes(f.role));
    assert.equal(entries.length, 1, `expected one ${roles.join('/')} file`);
    const entry = entries[0];
    assert(typeof entry.path === 'string' && entry.path !== '.' && entry.path !== '..' && path.basename(entry.path) === entry.path, 'invalid manifest path');
    const filename = await realpath(path.join(modelDir, entry.path));
    assert.equal(path.dirname(filename), modelDir, 'manifest file escapes model-dir');
    const bytes = await readFile(filename), sha256 = hash(bytes);
    assert(typeof entry.sha256 === 'string' && /^[a-f0-9]{64}$/i.test(entry.sha256), 'manifest must bind every input with SHA256');
    assert.equal(sha256, entry.sha256.toLowerCase(), `manifest checksum mismatch: ${entry.path}`);
    return { filename, bytes, sha256 };
  };
  const original = await file('inference-model');
  const sourceFile = await file('source-srcvocab', 'source-vocab');
  const targetFile = await file('source-trgvocab', 'source-vocab');
  const lexFile = await file('source-lex');
  const model: any = modelType.toObject(modelType.decode(original.bytes), { longs: Number, bytes: Buffer });
  assert(!(model.graph.initializer ?? []).some((t: any) => t.dataLocation === 1 || t.externalData?.length), 'external ONNX weights are unsupported');
  const source = await Tokenizer.load(sourceFile.filename);
  let target: Tokenizer | undefined;
  let baseline: ort.InferenceSession | undefined, candidate: ort.InferenceSession | undefined;
  try {
    target = await Tokenizer.load(targetFile.filename);
    assert.equal(hash(await readFile(sourceFile.filename)), sourceFile.sha256, 'source vocabulary changed while loading');
    assert.equal(hash(await readFile(targetFile.filename)), targetFile.sha256, 'target vocabulary changed while loading');
    const lex = parseLexicon(lexFile.bytes, target.size, source.size);
    const byteIds = byteTokenIds(targetFile.bytes, target.size);
    const shared = sourceFile.sha256 === targetFile.sha256;
    const encoder = model.graph.initializer.find((t: any) => t.name === 'encoder_Wemb') ?? model.graph.initializer.find((t: any) => t.name === 'Wemb');
    assert.equal(encoder?.dims[0], source.size, 'source embedding/vocabulary mismatch');
    const conversionStart = performance.now();
    const conversion = rewriteProjection(model);
    assert.equal(conversion.vocab, target.size, 'target embedding/vocabulary mismatch');
    const variant = modelType.encode(modelType.create(model)).finish();
    const output = await reserveOutput(values['output-dir']);
    // The entire output directory is new; exclusive writes also prevent replacing an original model.
    const variantPath = path.join(output, 'candidate-fp32.onnx');
    await writeFile(variantPath, variant, { flag: 'wx' });
    const conversionMs = performance.now() - conversionStart;
    const sessionOptions: ort.InferenceSession.SessionOptions = {
      executionProviders: ['cpu'], intraOpNumThreads: threads, interOpNumThreads: 1,
      executionMode: 'sequential', graphOptimizationLevel: 'all', logSeverityLevel: 3,
      // Idle sessions must not compete with the session being measured.
      extra: { session: { intra_op: { allow_spinning: '0' }, inter_op: { allow_spinning: '0' } } },
    };
    const sessionStart = performance.now();
    baseline = await ort.InferenceSession.create(original.filename, sessionOptions);
    candidate = await ort.InferenceSession.create(variantPath, sessionOptions);
    const sessionCreationMs = performance.now() - sessionStart;
    const metadata = {
      warning: '实验会改变译文；EOS 完成及与 baseline 一致均不代表翻译质量通过',
      modelDir, output, texts, threads, rounds, node: process.version, runtime: ort.env.versions, sessionOptions,
      hashes: { manifest: hash(manifestBytes), baseline: original.sha256, candidate: hash(variant), sourceVocab: sourceFile.sha256, targetVocab: targetFile.sha256, lex: lexFile.sha256, inputs: hash(JSON.stringify(texts)) },
      sourceEos: source.eos, targetEos: target.eos, sourceSize: source.size, targetSize: target.size,
      shared, byteIds, conversion, conversionMs, sessionCreationMs,
      timing: 'totalMs includes tokenization, candidate generation/validation, tensor creation, run, decode and disposal; excludes conversion/session creation and result file I/O',
    };
    const infer = async (text: string, kind: 'baseline' | 'candidate') => {
      const start = performance.now();
      const inputIds = [...source.encode(text), source.eos];
      const tokenizeMs = performance.now() - start;
      const candidateStart = performance.now();
      const candidates = kind === 'candidate' ? generateCandidates(lex, inputIds, target!.size, shared, target!.eos, byteIds) : undefined;
      if (candidates) validateRequest(inputIds, candidates, source.size, target!.size, target!.eos);
      else assert(inputIds.every(id => Number.isSafeInteger(id) && id >= 0 && id < source.size), 'invalid source IDs');
      const candidateMs = performance.now() - candidateStart;
      const feeds: Record<string, ort.Tensor> = {};
      let outputs: Record<string, ort.Tensor> = {};
      try {
        const tensorStart = performance.now();
        feeds.X = new ort.Tensor('int64', BigInt64Array.from(inputIds, BigInt), [inputIds.length]);
        feeds.eos_id = new ort.Tensor('int64', BigInt64Array.of(BigInt(target!.eos)), [1]);
        if (candidates) feeds.candidate_ids = new ort.Tensor('int64', BigInt64Array.from(candidates, BigInt), [candidates.length]);
        const tensorMs = performance.now() - tensorStart;
        const runStart = performance.now();
        outputs = await (kind === 'baseline' ? baseline! : candidate!).run(feeds);
        const runMs = performance.now() - runStart;
        assert(outputs.Y?.data instanceof BigInt64Array, 'expected int64 Y');
        const tokens = Array.from(outputs.Y.data, Number), eosIndex = tokens.indexOf(target!.eos);
        assert(tokens.length > 0 && tokens.length <= 1 + inputIds.length * 3, 'invalid output length');
        assert(tokens.every(id => Number.isSafeInteger(id) && id >= 0 && id < target!.size && id !== 1), 'invalid output IDs');
        assert(eosIndex < 0 || eosIndex === tokens.length - 1, 'output after EOS');
        if (candidates) { const selected = new Set(candidates); assert(tokens.every(id => selected.has(id)), 'invalid candidate mapping'); }
        const decoded = target!.decode(eosIndex < 0 ? tokens : tokens.slice(0, eosIndex));
        return { kind, inputIds, candidates, tokens, decoded, eosIndex, endedWithEos: eosIndex >= 0,
          status: eosIndex < 0 ? 'missing_eos' : 'completed',
          hitLengthLimit: eosIndex < 0 && tokens.length === 1 + inputIds.length * 3,
          tokenizeMs, candidateMs, tensorMs, runMs, totalMs: 0 };
      } finally {
        for (const tensor of [...Object.values(outputs), ...Object.values(feeds)]) tensor.dispose();
      }
    };
    const results: object[] = [];
    const resultsFile = path.join(output, 'results.jsonl');
    await writeFile(resultsFile, '', { flag: 'wx' });
    // Warm every text, then reverse each text's order between rounds, including even-sized corpora.
    for (let round = -1; round < rounds; round++) for (const [index, text] of texts.entries()) {
      const order = (round + index + 1) % 2 ? ['candidate', 'baseline'] as const : ['baseline', 'candidate'] as const;
      const measured = {} as Record<'baseline' | 'candidate', Awaited<ReturnType<typeof infer>>>;
      for (const kind of order) {
        const start = performance.now();
        const result = await infer(text, kind);
        result.totalMs = performance.now() - start;
        measured[kind] = result;
      }
      const comparable = measured.baseline.endedWithEos && measured.candidate.endedWithEos;
      const row = { phase: round < 0 ? 'warmup' : 'measure', round, index, text, order, ...measured,
        baselineMissingEos: !measured.baseline.endedWithEos, comparable,
        tokenExact: comparable ? JSON.stringify(measured.baseline.tokens) === JSON.stringify(measured.candidate.tokens) : null,
        decodedExact: comparable ? measured.baseline.decoded === measured.candidate.decoded : null };
      results.push(row);
      await appendFile(resultsFile, JSON.stringify(row) + '\n');
    }
    assert.equal(hash(await readFile(original.filename)), original.sha256, 'original model changed during benchmark');
    await writeFile(path.join(output, 'result.json'), JSON.stringify({ ...metadata, results }, null, 2) + '\n', { flag: 'wx' });
    console.log(JSON.stringify({ output, result: path.join(output, 'result.json'), pairs: results.length }));
  } finally {
    try { await baseline?.release(); } finally {
      try { await candidate?.release(); } finally { source.close(); target?.close(); }
    }
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  main().catch(error => { console.error(error); process.exitCode = 1; });
}
