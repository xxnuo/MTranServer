import assert from 'node:assert/strict';
import { copyFile, mkdir, mkdtemp, readFile, rm, stat, unlink, utimes, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { ensureConverted, fileHash, type ConversionSource, type SourceFiles } from '../src/converter/cache.js';
import { VERSION } from '../src/version/index.js';

function source(model: string, vocab: string): ConversionSource {
  return {
    files: { model, vocab_src: vocab, vocab_trg: vocab },
    records: [
      { fileType: 'model', attachment: { filename: path.basename(model) + '.zst', hash: 'model-source' } },
      { fileType: 'vocab', attachment: { filename: path.basename(vocab) + '.zst', hash: 'vocab-source' } },
    ],
  };
}

const args = process.argv.slice(2);
if (args[0] === '--child') {
  const [, root, model, vocab] = args;
  const input = source(model, vocab);
  await ensureConverted(root, 'de_en', input, async () => input.files);
} else {
  const [model, vocab] = args;
  if (!vocab) throw new Error('Usage: check-model-cache release.bin vocabulary.spm');
  const root = await mkdtemp(path.join(tmpdir(), 'mtran-cache-'));
  const pair = path.join(root, 'de_en');
  const sourceModel = path.join(pair, 'release.bin');
  const sourceVocab = path.join(pair, 'vocab.spm');
  await mkdir(pair);
  await copyFile(model, sourceModel);
  await copyFile(vocab, sourceVocab);
  const input = source(sourceModel, sourceVocab);
  let repairs = 0;
  const repair = async (): Promise<SourceFiles> => {
    repairs++;
    try { await stat(sourceModel); } catch { await copyFile(model, sourceModel); }
    try { await stat(sourceVocab); } catch { await copyFile(vocab, sourceVocab); }
    return input.files;
  };
  try {
    const [a, b] = await Promise.all([ensureConverted(root, 'de_en', input, repair), ensureConverted(root, 'de_en', input, repair)]);
    assert.equal(a.identity, b.identity);
    assert.equal(a.directory, pair);
    assert.equal(a.sourceVocab, sourceVocab);
    assert.equal(repairs, 1);

    const manifestPath = path.join(pair, 'manifest.json');
    const firstManifest = JSON.parse(await readFile(manifestPath, 'utf8'));
    assert.equal(firstManifest.mtranserverVersion, VERSION);
    assert.equal(typeof firstManifest.converterRevision, 'string');
    assert.equal(firstManifest.conversion, undefined);
    assert.deepEqual(firstManifest.files.map((file: any) => file.role), ['source-model', 'source-vocab', 'inference-model', 'manifest']);

    const before = (await stat(a.model)).mtimeMs;
    await ensureConverted(root, 'de_en', input, repair);
    assert.equal((await stat(a.model)).mtimeMs, before);
    assert.equal(repairs, 1);

    await writeFile(a.model, 'present but unchecked');
    await ensureConverted(root, 'de_en', input, repair);
    assert.equal(await readFile(a.model, 'utf8'), 'present but unchecked');
    assert.equal(repairs, 1);

    await unlink(a.model);
    await ensureConverted(root, 'de_en', input, repair);
    const expected = await fileHash(a.model);
    assert.equal(repairs, 2);

    const beforeSourceRepair = (await stat(a.model)).mtimeMs;
    await unlink(sourceModel);
    await ensureConverted(root, 'de_en', input, repair);
    assert.equal(await fileHash(a.model), expected);
    assert.equal((await stat(a.model)).mtimeMs, beforeSourceRepair);
    assert.equal(repairs, 3);

    const oldServer = JSON.parse(await readFile(manifestPath, 'utf8'));
    oldServer.mtranserverVersion = '0.0.0';
    await writeFile(manifestPath, JSON.stringify(oldServer));
    const beforeVersionRefresh = (await stat(a.model)).mtimeMs;
    await ensureConverted(root, 'de_en', input, repair);
    assert.equal((await stat(a.model)).mtimeMs, beforeVersionRefresh);
    assert.equal(JSON.parse(await readFile(manifestPath, 'utf8')).mtranserverVersion, VERSION);
    assert.equal(repairs, 3);

    const oldConverter = JSON.parse(await readFile(manifestPath, 'utf8'));
    oldConverter.mtranserverVersion = '0.0.0';
    oldConverter.converterRevision = 'old-converter';
    await writeFile(manifestPath, JSON.stringify(oldConverter));
    await ensureConverted(root, 'de_en', input, repair);
    assert.equal(repairs, 4);

    await writeFile(sourceModel, 'invalid model');
    const invalid = JSON.parse(await readFile(manifestPath, 'utf8'));
    invalid.mtranserverVersion = '0.0.0';
    invalid.converterRevision = 'old-converter';
    await writeFile(manifestPath, JSON.stringify(invalid));
    await assert.rejects(ensureConverted(root, 'de_en', input, repair));
    assert.equal(await fileHash(a.model), expected);
    await copyFile(model, sourceModel);

    await mkdir(pair + '.lock');
    await utimes(pair + '.lock', new Date(0), new Date(0));
    const child = () => new Promise<void>((resolve, reject) => {
      const script = fileURLToPath(import.meta.url);
      const runtimeArgs = process.versions.bun ? [script, '--child', root, sourceModel, sourceVocab] : ['--import', 'tsx', script, '--child', root, sourceModel, sourceVocab];
      const childProcess = spawn(process.execPath, runtimeArgs, { stdio: 'inherit' });
      childProcess.once('error', reject);
      childProcess.once('exit', code => code === 0 ? resolve() : reject(new Error(`child: ${code}`)));
    });
    await Promise.all([child(), child()]);
    assert.equal(await fileHash(a.model), expected);
    console.log('Cache: colocated files, manifest roles, existence reuse, missing-file repair, version refresh, parameter refresh, failure preservation and concurrency passed');
  } finally {
    await rm(root, { recursive: true, force: true });
  }
}
