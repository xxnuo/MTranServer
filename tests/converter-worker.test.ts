import { expect, test } from 'bun:test';
import { fileURLToPath } from 'node:url';
import { readFileSync } from 'node:fs';

test.each([
  [undefined, 'lex.bin', false],
  [false, 'lex.bin', false],
  [true, 'lex.bin', true],
  [true, undefined, false],
  ['true', 'lex.bin', false],
])('worker selects candidate graph only for explicit true and lex (%j, %j)', async (enabled, lex, expected) => {
  // Isolate module mocks so the rest of the suite continues using real converter modules.
  const script = `
    import { mock } from 'bun:test';
    import assert from 'node:assert/strict';
    let graphMode, loaded = false;
    const expected = ${expected};
    mock.module('./src/converter/release.ts', () => ({ readRelease: async () => ({ weights: new Map([['Wemb', {dims:[8,2]}]]), config: {'tied-embeddings-all':true} }) }));
    mock.module('./src/converter/graph.ts', () => ({ buildModel(_w,_c,_m,mode) { graphMode = mode; return new Uint8Array(); } }));
    mock.module('./src/core/tokenizer.ts', () => ({ Tokenizer: { load: async () => ({ size:8, eos:2, encode:()=>[0], close(){} }) } }));
    mock.module('./src/core/shortlist.ts', () => ({ Shortlist: { load: async () => { loaded = true; return { candidates:()=>[0,2] }; } } }));
    class Tensor { constructor(){} dispose(){} }
    mock.module('onnxruntime-node', () => ({ Tensor, InferenceSession: { create: async () => ({ run: async feeds => { assert.equal('candidate_ids' in feeds, expected); return {Y:{type:'int64',size:1,dispose(){}}}; }, release:async()=>{} }) } }));
    const workerSource = ${JSON.stringify(readFileSync(new URL('../src/converter/worker.ts', import.meta.url), 'utf8'))}
      .replace(/import \{ parentPort, workerData \} from 'node:worker_threads';/, ${JSON.stringify('const workerData = ')} + ${JSON.stringify(JSON.stringify({ lexicalShortlist: enabled, lex, model: 'fixture', output: 'unused', sourceVocab: 'src', targetVocab: 'dst' }))} + '; const parentPort = { postMessage(message) { assert.deepEqual(message, {ok:true}); } };')
      .replace("import { writeFile } from 'node:fs/promises';", 'const writeFile = async () => {};')
      .replaceAll("from './", "from './src/converter/")
      .replaceAll("from '../core/", "from './src/core/");
    const resolvedSource = workerSource.replaceAll("from './src/", "from '" + process.cwd() + "/src/");
    await import('data:text/javascript;base64,' + Buffer.from(new Bun.Transpiler({loader:'ts'}).transformSync(resolvedSource)).toString('base64'));
    assert.equal(graphMode, expected);
    assert.equal(loaded, expected);
  `;
  const child = Bun.spawn([process.execPath, '--eval', script], { cwd: pathRoot(), stdout: 'pipe', stderr: 'pipe' });
  const stderr = await new Response(child.stderr).text();
  expect(await child.exited, stderr).toBe(0);
});

function pathRoot() { return fileURLToPath(new URL('..', import.meta.url)); }
