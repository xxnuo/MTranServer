import assert from 'node:assert/strict';
import { mkdtemp, copyFile, readFile, writeFile, stat, rename, mkdir, rm, utimes } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { ensureConverted, fileHash } from '../src/converter/cache.js';
const args = process.argv.slice(2);
if (args[0] === '--child') {
  const [_, root, model, vocab] = args;
  await ensureConverted(root, 'de_en', async () => ({files:{model,vocab_src:vocab,vocab_trg:vocab},records:[]}));
} else {
  const [model, vocab] = args;
  if (!vocab) throw new Error('Usage: check-model-cache release.bin vocabulary.spm');
  const root = await mkdtemp(path.join(tmpdir(), 'mtran-cache-'));
  const source = path.join(root,'source.bin'), target=path.join(root,'converted'), pair=path.join(target,'de_en');
  await copyFile(model,source);
  const load=async()=>({files:{model:source,vocab_src:vocab,vocab_trg:vocab},records:[]});
  try {
    const [a,b]=await Promise.all([ensureConverted(target,'de_en',load),ensureConverted(target,'de_en',load)]);
    assert.equal(a.identity,b.identity);
    const before=(await stat(a.model)).mtimeMs;
    await ensureConverted(target,'de_en',load);assert.equal((await stat(a.model)).mtimeMs,before);
    const expected=await fileHash(a.model);
    await writeFile(a.model,'broken');await ensureConverted(target,'de_en',load);assert.equal(await fileHash(a.model),expected);
    await writeFile(source,'invalid model');await assert.rejects(ensureConverted(target,'de_en',load));assert.equal(await fileHash(a.model),expected);
    await copyFile(model,source);
    await rename(pair,pair+'.previous');await mkdir(pair+'.staging');await mkdir(pair+'.lock');await utimes(pair+'.lock',new Date(0),new Date(0));
    await ensureConverted(target,'de_en',load);assert.equal(await fileHash(a.model),expected);
    const manifest=JSON.parse(await readFile(path.join(pair,'manifest.json'),'utf8'));manifest.identity='old-algorithm';await writeFile(path.join(pair,'manifest.json'),JSON.stringify(manifest));
    const child=()=>new Promise<void>((resolve,reject)=>{const p=spawn(process.execPath,['--import','tsx',fileURLToPath(import.meta.url),'--child',target,source,vocab],{stdio:'inherit'});p.once('error',reject);p.once('exit',code=>code===0?resolve():reject(new Error(`child: ${code}`)));});
    await Promise.all([child(),child()]);assert.equal(await fileHash(a.model),expected);
    console.log('Cache: reuse, corruption, failure preservation, interrupted swap, stale lock, algorithm refresh and cross-process concurrency passed');
  } finally {await rm(root,{recursive:true,force:true});}
}
