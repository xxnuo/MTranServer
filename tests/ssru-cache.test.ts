import { afterEach, expect, mock, spyOn, test } from 'bun:test';
import { createHash } from 'node:crypto';
import { mkdir, mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import * as converter from '../src/converter/cache';
import { ensureNativeSsru, SSRU_CONVERSION_VERSION, SSRU_GRAPH_ABI } from '../src/converter/ssru-cache';

const root = path.resolve(fileURLToPath(new URL('..', import.meta.url)));
const directories: string[] = [];
afterEach(async () => {
  mock.restore();
  await Promise.all(directories.splice(0).map(directory => rm(directory, { recursive: true, force: true })));
});
async function fixture() {
  await mkdir(path.join(root, '.tmp'), { recursive: true });
  const directory = await mkdtemp(path.join(root, '.tmp/ssru-cache-test-')); directories.push(directory);
  const model = { directory, identity: 'ordinary-fast', model: path.join(directory, 'model.onnx'),
    sourceModel: path.join(directory, 'source.bin'), sourceVocab: path.join(directory, 'src.spm'),
    targetVocab: path.join(directory, 'dst.spm'), lex: path.join(directory, 'lex.bin') };
  await Promise.all(Object.entries(model).filter(([key]) => !['directory', 'identity'].includes(key)).map(([, file]) => writeFile(file, 'ordinary-fixture')));
  return model;
}
function worker() {
  return spyOn(converter, 'runWorker').mockImplementation(async (_source, _src, _dst, output) => { await writeFile(output, 'custom-graph'); });
}
async function child(script: string) {
  const process = Bun.spawn([Bun.which('bun')!, '--eval', script], { cwd: root, stdout: 'pipe', stderr: 'pipe' });
  const [stdout, stderr, code] = await Promise.all([new Response(process.stdout).text(), new Response(process.stderr).text(), process.exited]);
  expect(code, stdout + stderr).toBe(0);
}

test('native SSRU cache coalesces conversions under a lock, keeps ordinary graph intact, and checks content', async () => {
  const model = await fixture(), convert = worker();
  const results = await Promise.all(Array.from({ length: 5 }, () => ensureNativeSsru(model)));
  expect(new Set(results.map(result => result.model)).size).toBe(1); expect(convert).toHaveBeenCalledTimes(1);
  const result = results[0];
  expect(result.model).not.toBe(model.model); expect(result.identity).not.toBe(model.identity);
  expect(await readFile(model.model, 'utf8')).toBe('ordinary-fixture');
  const sourceHash = await converter.fileHash(model.sourceModel);
  expect(result.identity).toBe(createHash('sha256').update(JSON.stringify({ model: model.identity, sourceHash,
    abi: SSRU_GRAPH_ABI, version: SSRU_CONVERSION_VERSION })).digest('hex'));
  const call = convert.mock.calls[0];
  expect(call).toEqual([model.sourceModel, model.sourceVocab, model.targetVocab, expect.stringContaining(result.model + '.'), true, model.lex, undefined, true]);
  expect(call[3]).not.toBe(result.model);
  expect(JSON.parse(await readFile(result.model + '.json', 'utf8'))).toMatchObject({ identity: result.identity, sha256: await converter.fileHash(result.model) });
  expect((await readdir(model.directory)).some(name => name.endsWith('.tmp') || name.endsWith('.lock'))).toBe(false);
});

test('settled warm boot uses statSync fast-path without re-hashing source or cached onnx', async () => {
  const model = await fixture(), convert = worker();
  const sourceModelSha256 = await converter.fileHash(model.sourceModel);
  const withHash = { ...model, sourceModelSha256 };
  const first = await ensureNativeSsru(withHash);
  expect(convert).toHaveBeenCalledTimes(1);
  await new Promise(resolve => setTimeout(resolve, 35));
  const hashSpy = spyOn(converter, 'fileHash');
  const second = await ensureNativeSsru(withHash);
  expect(second).toEqual(first);
  expect(hashSpy).not.toHaveBeenCalled();
  await writeFile(first.model, 'corrupted-size-diff');
  await new Promise(resolve => setTimeout(resolve, 35));
  const rebuilt = await ensureNativeSsru(withHash);
  expect(rebuilt).toEqual(first);
  expect(convert).toHaveBeenCalledTimes(2);
});

test('cache identity changes with original identity and actual source hash', async () => {
  const model = await fixture(); worker();
  const original = await ensureNativeSsru(model);
  const changedModel = await ensureNativeSsru({ ...model, identity: 'updated-fast' });
  expect(changedModel.model).not.toBe(original.model);
  await writeFile(model.sourceModel, 'changed-model');
  const changedSource = await ensureNativeSsru(model);
  expect(changedSource.model).not.toBe(original.model);
  expect(await readFile(original.model, 'utf8')).toBe('custom-graph');
  expect(await readFile(model.model, 'utf8')).toBe('ordinary-fixture');
});

test.each(['graph', 'empty', 'missing-graph', 'manifest', 'missing-manifest', 'identity', 'checksum'])('damaged %s is rebuilt without touching ordinary model.onnx', async damage => {
  const model = await fixture(), convert = worker(), result = await ensureNativeSsru(model);
  if (damage === 'graph') await writeFile(result.model, 'bad-graph');
  if (damage === 'empty') await writeFile(result.model, '');
  if (damage === 'missing-graph') await rm(result.model);
  if (damage === 'manifest') await writeFile(result.model + '.json', 'not-json');
  if (damage === 'missing-manifest') await rm(result.model + '.json');
  if (damage === 'identity' || damage === 'checksum') await writeFile(result.model + '.json', JSON.stringify({ identity: damage === 'identity' ? 'old' : result.identity, sha256: 'bad' }));
  expect(await ensureNativeSsru(model)).toEqual(result); expect(convert).toHaveBeenCalledTimes(2);
  expect(await readFile(result.model, 'utf8')).toBe('custom-graph');
  expect(await readFile(model.model, 'utf8')).toBe('ordinary-fixture');
});

test.each(['conversion', 'empty', 'source-changed'])('failed %s conversion removes staged files, unlocks and can retry', async failure => {
  const model = await fixture(); const convert = worker();
  convert.mockImplementationOnce(async (_source, _src, _dst, output) => {
    await writeFile(output, failure === 'empty' ? '' : 'partial');
    if (failure === 'source-changed') await writeFile(model.sourceModel, 'changed while converting');
    if (failure === 'conversion') throw new Error('worker failed');
  });
  await expect(ensureNativeSsru(model)).rejects.toThrow(failure === 'conversion' ? 'worker failed' : failure === 'empty' ? 'Empty native SSRU graph' : 'source changed');
  expect((await readdir(model.directory)).some(name => name.startsWith('native-ssru-'))).toBe(false);
  await ensureNativeSsru(model); expect(convert).toHaveBeenCalledTimes(2);
  expect(await readFile(model.model, 'utf8')).toBe('ordinary-fixture');
});

test('missing source/lex or a source checksum mismatch is rejected before conversion', async () => {
  const model = await fixture(), convert = worker();
  await expect(ensureNativeSsru({ ...model, sourceModel: undefined })).rejects.toThrow('source model');
  await expect(ensureNativeSsru({ ...model, lex: undefined })).rejects.toThrow('shortlist');
  await expect(ensureNativeSsru({ ...model, sourceModelSha256: 'bad' })).rejects.toThrow('checksum mismatch');
  expect(convert).not.toHaveBeenCalled();
  const std = await ensureNativeSsru({ ...model, lex: undefined }, false);
  const fast = await ensureNativeSsru(model, true);
  expect(std.identity).not.toBe(fast.identity);
  expect(convert).toHaveBeenCalledTimes(2);
});

test('independent processes share one conversion and atomically publish the same cache', async () => {
  const model = await fixture();
  const script = `
    import { mock } from 'bun:test';
    import { createHash } from 'node:crypto';
    import { readFile, writeFile, appendFile } from 'node:fs/promises';
    mock.module('./src/converter/cache.ts', () => ({
      fileHash: async file => createHash('sha256').update(await readFile(file)).digest('hex'),
      runWorker: async (_source, _src, _dst, output) => {
        await appendFile(${JSON.stringify(path.join(model.directory, 'conversions'))}, 'once\\n');
        await new Promise(resolve => setTimeout(resolve, 30));
        await writeFile(output, 'cross-process-graph');
      }
    }));
    const { ensureNativeSsru } = await import('./src/converter/ssru-cache.ts');
    await ensureNativeSsru(${JSON.stringify(model)});
  `;
  await Promise.all([child(script), child(script)]);
  expect(await readFile(path.join(model.directory, 'conversions'), 'utf8')).toBe('once\n');
  const files = (await readdir(model.directory)).filter(name => name.endsWith('.onnx'));
  expect(files.length).toBe(2); expect(await readFile(model.model, 'utf8')).toBe('ordinary-fixture');
});

test.each(['valid', 'missing-lex', 'vocab-mismatch'])('native worker %s builds the custom graph and preserves vocabulary checks without plain ORT smoke', async kind => {
  const source = await readFile(new URL('../src/converter/worker.ts', import.meta.url), 'utf8');
  const data = { model: 'source', output: 'unused', sourceVocab: 'src', targetVocab: 'dst', nativeSsru: true,
    lexicalShortlist: true, lex: kind === 'missing-lex' ? undefined : 'lex' };
  const script = `
    import { mock } from 'bun:test';
    import assert from 'node:assert/strict';
    let built = false, closed = 0;
    mock.module('./src/converter/release.ts', () => ({ readRelease: async () => ({ weights:new Map([['Wemb',{dims:[8,2]}]]), config:{'tied-embeddings-all':true} }) }));
    mock.module('./src/converter/graph.ts', () => ({ buildModel(_w,_c,mode,shortlist,native) { assert.equal(mode,'qall'); assert.equal(shortlist,true); assert.equal(native,true); built=true; return new Uint8Array(); } }));
    mock.module('./src/core/tokenizer.ts', () => ({ Tokenizer: { load:async()=>({size:${kind === 'vocab-mismatch' ? 7 : 8},close(){closed++;}}) } }));
    mock.module('onnxruntime-node', () => { throw new Error('must not load ORT in the native graph conversion worker'); });
    const source = ${JSON.stringify(source)}
      .replace("import { parentPort, workerData } from 'node:worker_threads';", 'const workerData = ' + ${JSON.stringify(JSON.stringify(data))} + '; const parentPort = {postMessage(message){globalThis.ssruResponse=message;}};')
      .replace("import { writeFile } from 'node:fs/promises';", 'const writeFile = async () => {};')
      .replaceAll("from './", "from '${root}/src/converter/")
      .replaceAll("from '../core/", "from '${root}/src/core/");
    await import('data:text/javascript;base64,' + Buffer.from(new Bun.Transpiler({loader:'ts'}).transformSync(source)).toString('base64'));
    if (${JSON.stringify(kind)} === 'valid') { assert.deepEqual(globalThis.ssruResponse,{ok:true}); assert.equal(closed,2); }
    else assert.match(globalThis.ssruResponse.error, ${kind === 'missing-lex' ? '/lexical shortlist/' : '/Vocabulary size/'});
    assert.equal(built, ${kind !== 'missing-lex'});
  `;
  await child(script);
});
