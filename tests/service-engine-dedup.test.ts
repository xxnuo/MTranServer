import { expect, test } from 'bun:test';
import { fileURLToPath } from 'node:url';

test.each(['shared', 'html', 'failure', 'cache0', 'configuration', 'backend', 'tuning', 'fallback', 'cleanup'])('service in-flight deduplication: %s', async scenario => {
  // Child processes keep module mocks isolated from native engine tests.
  const script = `
    import { mock } from 'bun:test';
    import assert from 'node:assert/strict';
    import { mkdir, mkdtemp, writeFile, rm } from 'node:fs/promises';
    import path from 'node:path';
    import { getConfig, setConfig } from './src/config/index.ts';
    const scenario = ${JSON.stringify(scenario)};
    await mkdir('.tmp', {recursive:true});
    const root = await mkdtemp(path.resolve('.tmp/service-dedup-'));
    await mkdir(path.join(root, 'en_de'));
    await writeFile(path.join(root, 'en_de/manifest.json'), JSON.stringify({identity:'same-model'}));
    setConfig({modelDir:root, workersPerLanguage:1, workerIdleTimeout:3600, cacheSize:scenario === 'cache0' ? 0 : 1000});
    let release, calls = 0, reads = 0;
    const gate = new Promise(resolve => release = resolve);
    const instances = [], cache = new Map(), requests = [];
    const key = args => JSON.stringify(args.map(String));
    mock.module('./src/utils/cache.ts', () => ({
      readCache: args => { reads++; return getConfig().cacheSize > 0 ? cache.get(key(args)) ?? null : null; },
      writeCache: (result, args) => { if (getConfig().cacheSize > 0) cache.set(key(args), result); },
    }));
    mock.module('./src/core/engine.ts', () => ({ TranslationEngine: class {
      identity = 'same-model'; busy = false;
      backend = {provider:'cuda', cpuThreads:1, tuning:{spinning:true}};
      constructor() { this.id = instances.push(this); }
      async init() {}
      async translateAsync(text, options) {
        const attempt = ++calls;
        this.busy = true;
        try {
          await gate;
          if (scenario === 'failure' && attempt === 1) throw new Error('translation failed');
          if (scenario === 'fallback') this.backend.provider = 'cpu';
          return this.id + ':' + text + ':' + options.html;
        } finally { this.busy = false; }
      }
      async destroy() { if (scenario === 'cleanup' && this.busy) await gate; }
    } }));
    mock.module('./src/models/index.ts', () => ({hasLanguagePair: () => true, prepareModel: async () => ({identity:'same-model'})}));
    mock.module('./src/services/detector.ts', () => ({detectLanguage: async () => 'en', detectMultipleLanguages: async () => []}));
    const { translateWithPivot, cleanupAllEngines } = await import('./src/services/engine.ts');
    const translate = (html = false, text = 'text') => {
      const request = translateWithPivot('en', 'de', text, html);
      requests.push(request);
      request.catch(() => {});
      return request;
    };
    const waitForReads = async count => {
      for (let i = 0; i < 1000 && reads < count; i++) await new Promise(resolve => setImmediate(resolve));
      assert.ok(reads >= count, 'requests reached cache lookup');
    };
    try {
      const first = translate();
      await waitForReads(1);
      if (scenario === 'configuration') setConfig({workersPerLanguage:2});
      if (scenario === 'backend') instances[0].backend.cpuThreads = 2;
      if (scenario === 'tuning') instances[0].backend.tuning = {spinning:false};
      const second = translate(scenario === 'html');
      await waitForReads(2);
      const separate = ['html','cache0','configuration','backend','tuning'].includes(scenario);
      assert.equal(calls, separate ? 2 : 1, 'concurrent inference count');
      if (scenario === 'configuration') assert.equal(instances.length, 2);
      let closing;
      if (scenario === 'cleanup') closing = cleanupAllEngines();
      release();
      const results = await Promise.allSettled([first, second]);
      if (scenario === 'failure') {
        assert.deepEqual(results.map(result => result.status), ['rejected','rejected']);
        for (const result of results) assert.equal(result.reason.message, 'translation failed');
        assert.equal(await translate(), '1:text:false');
        assert.equal(calls, 2, 'failed in-flight entry is removed');
      } else {
        assert.deepEqual(results.map(result => result.status), ['fulfilled','fulfilled']);
        assert.equal(results[0].value, '1:text:false');
        assert.equal(results[1].value, scenario === 'configuration' ? '2:text:false' : scenario === 'html' ? '1:text:true' : '1:text:false');
        if (closing) {
          await closing;
          assert.equal(await translate(false, 'after-cleanup'), '2:after-cleanup:false');
          assert.equal(calls, 2);
        } else if (scenario === 'shared' || scenario === 'fallback') {
          assert.equal(await translate(), '1:text:false');
          assert.equal(calls, 1, 'successful translation uses the post-inference cache key');
        } else if (scenario === 'cache0') {
          assert.equal(await translate(), '1:text:false');
          assert.equal(calls, 3);
          assert.equal(cache.size, 0);
        }
      }
    } finally {
      release();
      await Promise.allSettled(requests);
      await cleanupAllEngines();
      await rm(root, {recursive:true,force:true});
    }
  `;
  const child = Bun.spawn([process.execPath, '--eval', script], { cwd: fileURLToPath(new URL('..', import.meta.url)), stdout: 'pipe', stderr: 'pipe' });
  const [, stderr, code] = await Promise.all([new Response(child.stdout).text(), new Response(child.stderr).text(), child.exited]);
  expect(code, stderr).toBe(0);
});
