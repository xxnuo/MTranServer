import { readFile, writeFile } from 'node:fs/promises';
import { Tokenizer } from '../src/core/tokenizer.js';

const [model, sourceVocab, targetVocab, corpusFile, referenceFile, output, backend] = process.argv.slice(2);
if (!output) throw new Error('Usage: verify-onnx model source.spm target.spm corpus.jsonl reference.jsonl output.json [wasm]');
const ort = backend === 'wasm' ? await import('onnxruntime-web/wasm') : await import('onnxruntime-node');
if (backend === 'wasm') ort.env.wasm.numThreads = 1;
const rows = (await readFile(corpusFile, 'utf8')).trim().split('\n').map(x => JSON.parse(x));
const refs = (await readFile(referenceFile, 'utf8')).trim().split('\n').map(x => JSON.parse(x));
if (rows.length !== refs.length) throw new Error('Corpus length mismatch');
const src = await Tokenizer.load(sourceVocab), dst = await Tokenizer.load(targetVocab);
const session = await ort.InferenceSession.create(backend === 'wasm' ? await readFile(model) : model, { intraOpNumThreads: 1, interOpNumThreads: 1, graphOptimizationLevel: 'all' });
const mismatches: unknown[] = [], results: unknown[] = [];
let tokenExact = 0, textExact = 0;
const started = performance.now();
try {
  for (let i = 0; i < rows.length; i++) {
    if (rows[i].index !== refs[i].index) throw new Error(`Corpus index mismatch: ${i}`);
    const ids = [...src.encode(rows[i].source), src.eos];
    const result = await session.run({ X: new ort.Tensor('int64', BigInt64Array.from(ids.map(BigInt)), [ids.length]), eos_id: new ort.Tensor('int64', BigInt64Array.of(BigInt(dst.eos)), [1]) });
    let tokens = Array.from(result.Y.data as BigInt64Array, Number);
    const end = tokens.indexOf(dst.eos); if (end >= 0) tokens = tokens.slice(0, end);
    const text = dst.decode(tokens), same = JSON.stringify(tokens) === JSON.stringify(refs[i].token_ids);
    if (same) tokenExact++; else mismatches.push({ index: i, source: rows[i].source, expected: refs[i].text, actual: text });
    if (text === refs[i].text) textExact++;
    results.push({ index: rows[i].index, token_ids: tokens, text });
    for (const tensor of Object.values(result)) tensor.dispose();
    if ((i + 1) % 100 === 0) console.log(`${i + 1}/${rows.length} token=${tokenExact} text=${textExact}`);
  }
} finally { await session.release(); src.close(); dst.close(); }
const summary = { runtime: ort.env.versions, count: rows.length, tokenExact, textExact, seconds: (performance.now() - started) / 1000, mismatches };
await writeFile(output, JSON.stringify(summary, null, 2));
await writeFile(output + '.jsonl', results.map(x => JSON.stringify(x)).join('\n') + '\n');
console.log(JSON.stringify({ ...summary, mismatches: mismatches.length }));
if (tokenExact !== rows.length || textExact !== rows.length) process.exitCode = 1;
