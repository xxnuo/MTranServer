import { expect, test } from 'bun:test';
import { fileURLToPath } from 'node:url';

test.each(['loaded', 'pending', 'pending-prepare', 'pending-failure', 'snapshot'])('service snapshots and rebuilds requested worker count: %s', async scenario => {
  const script = `
    import { mock } from 'bun:test';
    import assert from 'node:assert/strict';
    import { mkdir, mkdtemp, writeFile, rm } from 'node:fs/promises';
    import path from 'node:path';
    import { setConfig } from './src/config/index.ts';
    const scenario = ${JSON.stringify(scenario)};
    await mkdir('.tmp', {recursive:true});
    const root = await mkdtemp(path.resolve('.tmp/service-workers-'));
    await mkdir(path.join(root, 'en_de'));
    await writeFile(path.join(root, 'en_de/manifest.json'), JSON.stringify({identity:'same-model'}));
    let release, entered, instances = [], configs = [];
    const gate = new Promise(resolve => release = resolve);
    const started = new Promise(resolve => entered = resolve);
    const block = async () => { entered(); await gate; };
    mock.module('./src/core/engine.ts', () => ({ TranslationEngine: class {
      identity = 'same-model'; busy = false; destroyed = false;
      constructor() { instances.push(this); }
      async init(_model, config) {
        configs.push(config['max-concurrent-runs']);
        this.workers = config['max-concurrent-runs'];
        if ((scenario === 'pending' || scenario === 'pending-failure') && instances.length === 1) {
          await block();
          if (scenario === 'pending-failure') throw new Error('obsolete worker initialization');
        }
      }
      async translateAsync() { assert.equal(this.destroyed, false); return String(this.workers ?? 'auto'); }
      async destroy() { this.destroyed = true; }
    } }));
    let prepares = 0;
    mock.module('./src/models/index.ts', () => ({ hasLanguagePair: () => true, prepareModel: async () => {
      prepares++;
      if (scenario === 'pending-prepare' && prepares === 1) await block();
      if (scenario === 'snapshot' && prepares === 1) setConfig({workersPerLanguage:4});
      return {identity:'same-model'};
    }}));
    mock.module('./src/services/detector.ts', () => ({ detectLanguage: async () => 'en', detectMultipleLanguages: async () => [] }));
    mock.module('./src/utils/cache.ts', () => ({ readCache: () => null, writeCache: () => {} }));
    const { translateWithPivot, cleanupAllEngines } = await import('./src/services/engine.ts');
    setConfig({modelDir:root, workersPerLanguage:0, fast:true, workerIdleTimeout:3600, cacheSize:0});
    const translate = () => translateWithPivot('en', 'de', 'text');
    try {
      if (scenario.startsWith('pending')) {
        const first = translate(); await started;
        setConfig({workersPerLanguage:4});
        const second = translate(); release();
        assert.deepEqual(await Promise.all([first,second]), ['4','4']);
        assert.deepEqual(configs, [undefined,4]);
        if (scenario !== 'pending-failure') assert.equal(instances[0].destroyed,true);
      } else if (scenario === 'snapshot') {
        assert.equal(await translate(),'4');
        assert.deepEqual(configs,[undefined,4]);
        assert.equal(instances[0].destroyed,true);
      } else {
        assert.equal(await translate(),'auto');
        assert.equal(await translate(),'auto');
        for (const count of [1,4,0]) {
          const previous = instances.at(-1);
          setConfig({workersPerLanguage:count});
          assert.equal(await translate(),count ? String(count) : 'auto');
          assert.equal(previous.destroyed,true);
          assert.equal(await translate(),count ? String(count) : 'auto');
        }
        assert.deepEqual(configs,[undefined,1,4,undefined]);
      }
    } finally { release(); await cleanupAllEngines(); await rm(root,{recursive:true,force:true}); }
  `;
  const child = Bun.spawn([process.execPath, '--eval', script], { cwd: fileURLToPath(new URL('..', import.meta.url)), stdout: 'pipe', stderr: 'pipe' });
  const [, stderr, code] = await Promise.all([new Response(child.stdout).text(), new Response(child.stderr).text(), child.exited]);
  expect(code, stderr).toBe(0);
});
