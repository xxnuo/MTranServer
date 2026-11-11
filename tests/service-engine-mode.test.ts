import { expect, test } from 'bun:test';
import { fileURLToPath } from 'node:url';

test.each(['loaded', 'pending', 'pending-prepare', 'snapshot', 'missing-lex', 'pending-failure'])('service engine follows requested shortlist mode: %s', async scenario => {
  // Keep service/module mocks isolated from the real engine and converter tests.
  const script = `
    import { mock } from 'bun:test';
    import assert from 'node:assert/strict';
    import { mkdir, mkdtemp, writeFile, rm } from 'node:fs/promises';
    import path from 'node:path';
    import { getConfig, setConfig } from './src/config/index.ts';
    const scenario = ${JSON.stringify(scenario)};
    const temporary = path.resolve('.tmp');
    await mkdir(temporary, {recursive:true});
    const root = await mkdtemp(path.join(temporary, 'service-mode-'));
    await mkdir(path.join(root, 'en_de'));
    let prepared = [], instances = [], gate;
    let notifyStarted;
    const entered = new Promise(resolve => notifyStarted = resolve);
    const released = new Promise(resolve => gate = resolve);
    mock.module('./src/core/engine.ts', () => ({ TranslationEngine: class {
      identity = ''; busy = false; destroyed = false; mode = false;
      constructor() { instances.push(this); }
      async init(model) {
        this.identity = model.identity;
        this.mode = model.mode;
        if (scenario.startsWith('pending') && instances.length === 1) {
          notifyStarted();
          await released;
          if (scenario === 'pending-failure') throw new Error('obsolete initialization failed');
        }
      }
      async translateAsync() { assert.equal(this.destroyed, false); return this.mode ? 'shortlist' : 'K9'; }
      async destroy() { this.destroyed = true; }
    } }));
    mock.module('./src/models/index.ts', () => ({
      initRecords: async () => {}, downloadModel: async () => {}, hasLanguagePair: () => true,
      prepareModel: async (_to, _from, version, requested) => {
        assert.equal(version, undefined);
        assert.equal(typeof requested, 'boolean');
        if (scenario === 'snapshot') {
          setConfig({lexicalShortlist:true});
          await Promise.resolve();
          setConfig({lexicalShortlist:false});
        }
        prepared.push(requested);
        if (scenario === 'pending-prepare' && prepared.length === 1) { notifyStarted(); await released; }
        const mode = scenario === 'missing-lex' ? false : requested;
        const identity = mode ? 'shortlist' : 'K9';
        await writeFile(path.join(root, 'en_de/manifest.json'), JSON.stringify({identity}));
        return {identity, mode};
      },
    }));
    mock.module('./src/services/detector.ts', () => ({ detectLanguage: async () => 'en', detectMultipleLanguages: async () => [] }));
    mock.module('./src/utils/cache.ts', () => ({ readCache: () => null, writeCache: () => {} }));
    const { translateWithPivot, cleanupAllEngines } = await import('./src/services/engine.ts');
    const { MTran } = await import('./src/mtran.ts');
    const translate = () => translateWithPivot('en', 'de', 'text');
    setConfig({ modelDir:root, lexicalShortlist:scenario !== 'snapshot', workerIdleTimeout:3600, cacheSize:0 });
    try {
      if (scenario === 'snapshot') {
        assert.equal(await translate(), 'K9');
        assert.equal(await translate(), 'K9');
        assert.deepEqual(prepared, [false]);
      } else if (scenario.startsWith('pending')) {
        const old = translate();
        await entered;
        setConfig({lexicalShortlist:false});
        const current = translate();
        gate();
        assert.deepEqual(await Promise.all([old, current]), ['K9','K9']);
        assert.deepEqual(prepared, [true,false]);
        if (scenario !== 'pending-failure') assert.equal(instances[0].destroyed, true);
        assert.equal(await translate(), 'K9');
        assert.deepEqual(prepared, [true,false]);
        // Also switch from a pending K9 initialization to shortlist.
        await cleanupAllEngines();
        instances = []; prepared = [];
        let resume, notify;
        const blocked = new Promise(resolve => resume = resolve);
        const ready = new Promise(resolve => notify = resolve);
        (await import('./src/core/engine.ts')).TranslationEngine.prototype.init = async function(model) {
          this.identity = model.identity; this.mode = model.mode;
          if (instances.length === 1) { notify(); await blocked; }
        };
        const first = translate();
        await ready;
        new MTran({fast:true});
        const second = translate();
        resume();
        assert.deepEqual(await Promise.all([first,second]), ['shortlist','shortlist']);
        assert.deepEqual(prepared, [false,true]);
        assert.equal(instances[0].destroyed, true);
      } else {
        const expected = scenario === 'missing-lex' ? 'K9' : 'shortlist';
        assert.equal(await translate(), expected);
        assert.equal(await translate(), expected);
        assert.deepEqual(prepared, [true]);
        setConfig({lexicalShortlist:false});
        assert.equal(await translate(), 'K9');
        assert.equal(instances[0].destroyed, true);
        new MTran({fast:true});
        assert.equal(await translate(), expected);
        assert.equal(await translate(), expected);
        assert.deepEqual(prepared, [true,false,true]);
        assert.equal(instances[1].destroyed, true);
      }
    } finally { gate(); await cleanupAllEngines(); await rm(root, {recursive:true,force:true}); }
  `;
  const child = Bun.spawn([process.execPath, '--eval', script], { cwd: fileURLToPath(new URL('..', import.meta.url)), stdout: 'pipe', stderr: 'pipe' });
  const [, stderr, code] = await Promise.all([new Response(child.stdout).text(), new Response(child.stderr).text(), child.exited]);
  expect(code, stderr).toBe(0);
});
