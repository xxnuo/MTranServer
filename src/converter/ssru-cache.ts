import fs from 'node:fs/promises';
import { statSync } from 'node:fs';
import path from 'node:path';
import { createHash, randomUUID } from 'node:crypto';
import { lock } from 'proper-lockfile';
import { fileHash, runWorker, type ConvertedModel } from './cache.js';

export const SSRU_CONVERSION_VERSION = 'marian-qall-native-ssru-v3';
export const SSRU_GRAPH_ABI = 'com.mtran::SsruTranslate:1/X,eos,candidates,compact-int8-weights';

interface StatToken {
  ino: string;
  size: string;
  mtimeNs: string;
}

function readStat(filePath: string): { token: StatToken; settled: boolean } | null {
  try {
    const st = statSync(filePath, { bigint: true, throwIfNoEntry: false });
    if (!st) return null;
    const mtimeMs = Number(st.mtimeNs / 1_000_000n);
    return {
      token: { ino: st.ino.toString(), size: st.size.toString(), mtimeNs: st.mtimeNs.toString() },
      settled: mtimeMs + 25 < Date.now(),
    };
  } catch {
    return null;
  }
}

function sameStat(a: StatToken | undefined, b: StatToken | undefined): boolean {
  return !!a && !!b && a.ino === b.ino && a.size === b.size && a.mtimeNs === b.mtimeNs;
}

function makeIdentity(modelIdentity: string, sourceHash: string, shortlist: boolean): string {
  return createHash('sha256').update(JSON.stringify({
    model: modelIdentity,
    sourceHash,
    ...(shortlist ? {} : { shortlist: false }),
    abi: SSRU_GRAPH_ABI,
    version: SSRU_CONVERSION_VERSION,
  })).digest('hex');
}

export async function ensureNativeSsru(model: ConvertedModel, shortlist = true): Promise<{ model: string; identity: string }> {
  if (!model.sourceModel || (shortlist && !model.lex)) throw new Error('Native SSRU requires the source model and lexical shortlist');
  if (model.sourceModelSha256) {
    const identity = makeIdentity(model.identity, model.sourceModelSha256, shortlist);
    const output = path.join(model.directory, `native-ssru-v3-${identity}.onnx`);
    const manifest = output + '.json';
    const srcStat = readStat(model.sourceModel);
    const outStat = readStat(output);
    if (srcStat?.settled && outStat?.settled && BigInt(outStat.token.size) > 0n) {
      try {
        const cached = JSON.parse(await fs.readFile(manifest, 'utf8'));
        if (cached.identity === identity && sameStat(cached.sourceStat, srcStat.token) && sameStat(cached.outputStat, outStat.token)) {
          return { model: output, identity };
        }
      } catch { /* Fall through to full locked verification. */ }
    }
  }
  const sourceHash = await fileHash(model.sourceModel);
  if (model.sourceModelSha256 && sourceHash !== model.sourceModelSha256) throw new Error('Native SSRU source model checksum mismatch');
  const identity = makeIdentity(model.identity, sourceHash, shortlist);
  const output = path.join(model.directory, `native-ssru-v3-${identity}.onnx`);
  const manifest = output + '.json';
  await fs.mkdir(model.directory, { recursive: true });
  const unlock = await lock(output, { realpath: false, stale: 30000, update: 5000,
    retries: { forever: true, minTimeout: 100, maxTimeout: 1000 } });
  const staged = output + `.${randomUUID()}.tmp`;
  try {
    if (await fileHash(model.sourceModel) !== sourceHash) throw new Error('Native SSRU source changed before conversion');
    try {
      const cached = JSON.parse(await fs.readFile(manifest, 'utf8'));
      const srcStat = readStat(model.sourceModel);
      const outStat = readStat(output);
      if (cached.identity === identity && outStat && BigInt(outStat.token.size) > 0n) {
        if (srcStat?.settled && outStat.settled && sameStat(cached.sourceStat, srcStat.token) && sameStat(cached.outputStat, outStat.token)) {
          return { model: output, identity };
        }
        if (cached.sha256 === await fileHash(output)) return { model: output, identity };
      }
    } catch { /* Missing or corrupt caches are rebuilt under the same lock. */ }
    await runWorker(model.sourceModel, model.sourceVocab, model.targetVocab, staged, shortlist, shortlist ? model.lex : undefined, undefined, true);
    if (!(await fs.stat(staged)).size) throw new Error('Empty native SSRU graph');
    if (await fileHash(model.sourceModel) !== sourceHash) throw new Error('Native SSRU source changed during conversion');
    const sha256 = await fileHash(staged);
    const sourceStat = readStat(model.sourceModel)?.token;
    const outputStat = readStat(staged)?.token;
    await fs.writeFile(staged + '.json', JSON.stringify({ identity, sha256, ...(sourceStat ? { sourceStat } : {}), ...(outputStat ? { outputStat } : {}) }) + '\n');
    await fs.rename(staged, output);
    await fs.rename(staged + '.json', manifest);
    return { model: output, identity };
  } finally {
    try { await fs.rm(staged, { force: true }); await fs.rm(staged + '.json', { force: true }); }
    finally { await unlock(); }
  }
}
