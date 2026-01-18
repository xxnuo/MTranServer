import { test, expect } from 'bun:test';
import { createHash } from 'node:crypto';
import { mkdir, mkdtemp, writeFile, rm } from 'node:fs/promises';
import path from 'node:path';
import { ensureConverted, CONVERSION_VERSION, CONVERSION_PARAMETERS } from '../src/converter/cache';
import { VERSION } from '../src/version';

const shortlistConversion = { version: 'marian-qall-shortlist-v2', parameters: { ...CONVERSION_PARAMETERS, projection: 'lexical-fp32-byte-complete' } };
const hash = (value: unknown) => createHash('sha256').update(JSON.stringify(value)).digest('hex');
test('converter revision invalidates same-app-version caches and lex records participate in identity', async () => {
  const temporary = path.resolve(import.meta.dir, '../.tmp');
  await mkdir(temporary, { recursive: true });
  const root = await mkdtemp(path.join(temporary, 'conversion-cache-'));
  const pair = 'en_zh-Hans', target = path.join(root, pair);
  await mkdir(target);
  const files = { model: path.join(target, 'model.bin'), vocab_src: path.join(target, 'source.spm'), vocab_trg: path.join(target, 'target.spm'), lex: path.join(target, 'lex.bin') };
  for (const file of [...Object.values(files), path.join(target, 'model.onnx')]) await writeFile(file, 'fixture');
  const records = [{ fileType: 'lex', decompressedHash: 'first' }];
  const manifest = { schemaVersion: 1, mtranserverVersion: VERSION, converterRevision: 'old', identity: 'old-model', lexSha256: createHash('sha256').update('fixture').digest('hex'), sourceRevision: hash(records), files: [{ path: 'model.onnx', required: true }] };
  let repairs = 0;
  const repair = async () => { repairs++; throw new Error('repair requested'); };
  try {
    await writeFile(path.join(target, 'manifest.json'), JSON.stringify(manifest));
    await expect(ensureConverted(root, pair, { files, records }, repair, true)).rejects.toThrow('repair requested');
    expect(repairs).toBe(1);
    manifest.converterRevision = hash(shortlistConversion);
    await writeFile(path.join(target, 'manifest.json'), JSON.stringify(manifest));
    const reused = await ensureConverted(root, pair, { files, records }, repair, true);
    expect(reused.lex).toBe(files.lex);
    expect(repairs).toBe(1);
    await expect(ensureConverted(root, pair, { files, records: [{ ...records[0], decompressedHash: 'changed' }] }, repair, true)).rejects.toThrow('repair requested');
    expect(repairs).toBe(2);
    await writeFile(files.lex, 'changed-content-same-records');
    await expect(ensureConverted(root, pair, { files, records }, repair, true)).rejects.toThrow('repair requested');
    expect(repairs).toBe(3);
  } finally { await rm(root, { recursive: true, force: true }); }
});

test('default K9 reuses legacy metadata, ignores lex changes, and mode switches invalidate both ways', async () => {
  const temporary = path.resolve(import.meta.dir, '../.tmp');
  await mkdir(temporary, { recursive: true });
  const root = await mkdtemp(path.join(temporary, 'conversion-modes-'));
  const pair = 'en_de', target = path.join(root, pair);
  await mkdir(target);
  const files = { model: path.join(target, 'model.bin'), vocab_src: path.join(target, 'src.spm'), vocab_trg: path.join(target, 'dst.spm'), lex: path.join(target, 'missing.lex') };
  for (const file of [files.model, files.vocab_src, files.vocab_trg, path.join(target, 'model.onnx')]) await writeFile(file, 'fixture');
  const records = [{ fileType: 'model', decompressedHash: 'model' }, { fileType: 'lex', decompressedHash: 'lex' }];
  const manifest = { schemaVersion: 1, mtranserverVersion: 'old', identity: 'legacy-k9', sourceRevision: hash(records.slice(0, 1)), conversion: { version: CONVERSION_VERSION, parameters: CONVERSION_PARAMETERS }, files: [{ path: 'model.onnx', required: true }] };
  const repair = async (): Promise<never> => { throw new Error('reconversion required'); };
  try {
    await writeFile(path.join(target, 'manifest.json'), JSON.stringify(manifest));
    expect(await ensureConverted(root, pair, { files, records }, repair)).toMatchObject({ identity: 'legacy-k9' });
    expect((await ensureConverted(root, pair, { files, records }, repair)).lex).toBeUndefined();
    await expect(ensureConverted(root, pair, { files, records }, repair, true)).rejects.toThrow('reconversion required');
    const { lex, ...withoutLex } = files;
    expect((await ensureConverted(root, pair, { files: withoutLex, records }, repair, true)).identity).toBe('legacy-k9');
    await writeFile(lex, 'fixture');
    await writeFile(path.join(target, 'manifest.json'), JSON.stringify({ ...manifest, identity: 'candidate-graph', conversion: shortlistConversion, sourceRevision: hash([...records].sort((a, b) => JSON.stringify(a).localeCompare(JSON.stringify(b)))), lexSha256: createHash('sha256').update('fixture').digest('hex') }));
    expect((await ensureConverted(root, pair, { files, records }, repair, true)).lex).toBe(lex);
    await expect(ensureConverted(root, pair, { files, records }, repair)).rejects.toThrow('reconversion required');
    // Same application version must not turn an unknown converter into a compatible cache.
    await writeFile(path.join(target, 'manifest.json'), JSON.stringify({ ...manifest, mtranserverVersion: VERSION, conversion: undefined, converterRevision: 'unknown' }));
    await expect(ensureConverted(root, pair, { files, records }, repair)).rejects.toThrow('reconversion required');
  } finally { await rm(root, { recursive: true, force: true }); }
});

test('pending conversions coalesce only matching requests and reject concurrent mode/source conflicts', async () => {
  const temporary = path.resolve(import.meta.dir, '../.tmp');
  await mkdir(temporary, { recursive: true });
  const root = await mkdtemp(path.join(temporary, 'conversion-pending-'));
  const files = { model: 'missing', vocab_src: 'missing', vocab_trg: 'missing', lex: 'missing' };
  const source = { files, records: [] };
  let release!: () => void;
  const blocked = new Promise<void>(resolve => { release = resolve; });
  const repair = async (): Promise<never> => { await blocked; throw new Error('stopped fixture'); };
  try {
    const first = ensureConverted(root, 'en_de', source, repair);
    expect(ensureConverted(root, 'en_de', source, repair)).toBe(first);
    await expect(ensureConverted(root, 'en_de', source, repair, true)).rejects.toThrow('Conflicting conversion');
    await expect(ensureConverted(root, 'en_de', { files, records: [{ fileType: 'model' }] }, repair)).rejects.toThrow('Conflicting conversion');
    release();
    await expect(first).rejects.toThrow('stopped fixture');
    await expect(ensureConverted(root, 'en_de', source, repair, true)).rejects.toThrow('stopped fixture');
  } finally { release(); await rm(root, { recursive: true, force: true }); }
});
