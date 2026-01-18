import fs from 'node:fs/promises';
import path from 'node:path';
import { createHash, randomUUID } from 'node:crypto';
import { lock } from 'proper-lockfile';
import { fileHash, runWorker, type ConvertedModel } from './cache.js';

export const SSRU_CONVERSION_VERSION = 'marian-qall-native-ssru-v1';
export const SSRU_GRAPH_ABI = 'com.mtran::SsruDecode:1/X,eos,candidates,K*,V*,sorted-fp32-weights';

export async function ensureNativeSsru(model: ConvertedModel): Promise<{ model: string; identity: string }> {
  if (!model.sourceModel || !model.lex) throw new Error('Native SSRU requires the source model and lexical shortlist');
  const sourceHash = await fileHash(model.sourceModel);
  if (model.sourceModelSha256 && sourceHash !== model.sourceModelSha256) throw new Error('Native SSRU source model checksum mismatch');
  const identity = createHash('sha256').update(JSON.stringify({ model: model.identity, sourceHash,
    abi: SSRU_GRAPH_ABI, version: SSRU_CONVERSION_VERSION })).digest('hex');
  const output = path.join(model.directory, `native-ssru-${identity}.onnx`);
  const manifest = output + '.json';
  await fs.mkdir(model.directory, { recursive: true });
  const unlock = await lock(output, { realpath: false, stale: 30000, update: 5000,
    retries: { forever: true, minTimeout: 100, maxTimeout: 1000 } });
  const staged = output + `.${randomUUID()}.tmp`;
  try {
    if (await fileHash(model.sourceModel) !== sourceHash) throw new Error('Native SSRU source changed before conversion');
    try {
      const cached = JSON.parse(await fs.readFile(manifest, 'utf8'));
      if (cached.identity === identity && (await fs.stat(output)).size > 0 && cached.sha256 === await fileHash(output))
        return { model: output, identity };
    } catch { /* Missing or corrupt caches are rebuilt under the same lock. */ }
    await runWorker(model.sourceModel, model.sourceVocab, model.targetVocab, staged, true, model.lex, undefined, true);
    if (!(await fs.stat(staged)).size) throw new Error('Empty native SSRU graph');
    if (await fileHash(model.sourceModel) !== sourceHash) throw new Error('Native SSRU source changed during conversion');
    const sha256 = await fileHash(staged);
    await fs.writeFile(staged + '.json', JSON.stringify({ identity, sha256 }) + '\n');
    await fs.rename(staged, output);
    await fs.rename(staged + '.json', manifest);
    return { model: output, identity };
  } finally {
    try { await fs.rm(staged, { force: true }); await fs.rm(staged + '.json', { force: true }); }
    finally { await unlock(); }
  }
}
