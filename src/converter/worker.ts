import { parentPort, workerData } from 'node:worker_threads';
import { writeFile } from 'node:fs/promises';
import * as ort from 'onnxruntime-node';
import { readRelease } from './release.js';
import { buildModel } from './graph.js';
import { Tokenizer } from '../core/tokenizer.js';

try {
  const { weights, config } = await readRelease(workerData.model);
  await writeFile(workerData.output, buildModel(weights, config, 'qall'));
  const src = await Tokenizer.load(workerData.sourceVocab);
  let dst: Tokenizer | undefined, session: ort.InferenceSession | undefined;
  try {
    dst = await Tokenizer.load(workerData.targetVocab);
    const sourceEmbedding = config['tied-embeddings-all'] ? 'Wemb' : 'encoder_Wemb';
    const targetEmbedding = config['tied-embeddings-all'] ? 'Wemb' : 'decoder_Wemb';
    if (src.size !== weights.get(sourceEmbedding)?.dims[0] || dst.size !== weights.get(targetEmbedding)?.dims[0]) throw new Error('Vocabulary size does not match model embeddings');
    session = await ort.InferenceSession.create(workerData.output, { intraOpNumThreads: 1, interOpNumThreads: 1, graphOptimizationLevel: 'all' });
    const ids = [...src.encode('Hello.'), src.eos];
    const result = await session.run({ X: new ort.Tensor('int64', BigInt64Array.from(ids.map(BigInt)), [ids.length]), eos_id: new ort.Tensor('int64', BigInt64Array.of(BigInt(dst.eos)), [1]) });
    if (result.Y.type !== 'int64' || result.Y.size < 1) throw new Error('Invalid conversion smoke-test output');
    for (const tensor of Object.values(result)) tensor.dispose();
  } finally { await session?.release(); src.close(); dst?.close(); }
  parentPort!.postMessage({ ok: true });
} catch (error) { parentPort!.postMessage({ error: error instanceof Error ? error.message : String(error) }); }
