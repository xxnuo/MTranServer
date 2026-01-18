import { expect, test } from 'bun:test';
import { fileURLToPath } from 'node:url';

async function isolated(script: string) {
  const child = Bun.spawn([process.execPath, '--eval', script], {
    cwd: fileURLToPath(new URL('..', import.meta.url)), stdout: 'pipe', stderr: 'pipe',
  });
  const [stdout, stderr, code] = await Promise.all([
    new Response(child.stdout).text(), new Response(child.stderr).text(), child.exited,
  ]);
  expect(code, stdout + stderr).toBe(0);
}

test.each(['idle', 'concurrent', 'failure', 'manifest-failure', 'shutdown', 'shutdown-prepare', 'mode', 'workers', 'late-mode', 'late-workers', 'disabled', 'missing-lex', 'empty'])(
  'startup warmup shares engines, stays local and respects lifecycle: %s', async scenario => isolated(`
    import { mock, spyOn } from 'bun:test';
    import assert from 'node:assert/strict';
    import { mkdir, mkdtemp, writeFile, readFile, rm } from 'node:fs/promises';
    import path from 'node:path';
    import { getConfig, setConfig } from './src/config/index.ts';
    const scenario = ${JSON.stringify(scenario)};
    await mkdir('.tmp', {recursive:true});
    const root = await mkdtemp(path.resolve('.tmp/startup-warmup-'));
    const pairs = ['en_de', 'fr_en'];
    for (const pair of [...pairs, 'ja_en', 'not-a-pair']) {
      await mkdir(path.join(root, pair));
      if (pairs.includes(pair) && scenario !== 'empty') await writeFile(path.join(root, pair, 'manifest.json'), JSON.stringify({identity:'fast'}));
    }
    if (scenario === 'manifest-failure') await writeFile(path.join(root, 'en_de/manifest.json'), '{broken');
    let now = 0, next = 0;
    const timers = new Map();
    spyOn(globalThis, 'setTimeout').mockImplementation((callback, delay) => {
      const timer = ++next;
      timers.set(timer, {callback, at:now+delay, delay});
      return timer;
    });
    spyOn(globalThis, 'clearTimeout').mockImplementation(timer => { timers.delete(timer); });
    const tick = () => new Promise(resolve => setImmediate(resolve));
    const advance = async milliseconds => {
      now += milliseconds;
      for (const [timer, item] of [...timers]) if (item.at <= now && timers.delete(timer)) item.callback();
      await tick();
    };
    let enter, release;
    const entered = new Promise(resolve => enter = resolve);
    const gate = new Promise(resolve => release = resolve);
    const instances = [], prepares = [], configs = [], warnings = [];
    mock.module('./src/logger/index.ts', () => ({info:()=>{}, debug:()=>{}, error:()=>{}, warn:message=>warnings.push(message)}));
    mock.module('./src/core/engine.ts', () => ({ TranslationEngine: class {
      busy = false; destroyed = 0; backend = {};
      constructor() { instances.push(this); }
      async init(model, config) {
        this.identity = model.identity;
        configs.push(config);
        this.backend = {provider:'cpu', runtime:model.lex && process.env.MT_NATIVE_SSRU !== '0' ? 'native-ssru' : 'native-async'};
        if ((['idle','late-mode','late-workers'].includes(scenario) && instances.length === 2) ||
          (['concurrent','shutdown','mode','workers'].includes(scenario) && instances.length === 1)) {
          enter(); await gate;
        }
        if (scenario === 'failure' && instances.length === 1) throw new Error('invalid local graph');
      }
      async translateAsync() { assert.equal(this.destroyed, 0); return this.identity; }
      async destroy() { this.destroyed++; }
    }}));
    mock.module('./src/models/index.ts', () => ({
      hasLanguagePair: () => true,
      prepareModel: async (to, from, version, fast, localOnly) => {
        prepares.push({pair:from+'_'+to, fast, localOnly});
        assert.equal(version, undefined);
        if (scenario === 'shutdown-prepare' && prepares.length === 1) { enter(); await gate; }
        if (scenario === 'manifest-failure') JSON.parse(await readFile(path.join(root, from+'_'+to, 'manifest.json'), 'utf8'));
        const identity = fast ? 'fast' : 'standard';
        await writeFile(path.join(root, from+'_'+to, 'manifest.json'), JSON.stringify({identity}));
        return {identity, lex:fast && scenario !== 'missing-lex' ? 'lex' : undefined};
      },
    }));
    mock.module('./src/services/detector.ts', () => ({detectLanguage:async()=> 'en', detectMultipleLanguages:async()=> []}));
    mock.module('./src/utils/cache.ts', () => ({readCache:()=>null, writeCache:()=>{}}));
    const {warmupEngines, translateWithPivot, cleanupAllEngines, getEngineRuntimes} = await import('./src/services/engine.ts');
    setConfig({modelDir:root, fast:scenario !== 'disabled', workerIdleTimeout:60, workersPerLanguage:2, cacheSize:0, logConsole:false});
    const warm = warmupEngines();
    const result = warm.then(() => null, error => error);
    assert.equal(warmupEngines(), warm, 'concurrent startup shares one warmup');
    try {
      if (scenario === 'idle') {
        await entered;
        assert.equal(getEngineRuntimes().length, 1);
        await advance(61000);
        assert.equal(timers.size, 0, 'no idle timer during startup');
        assert.equal(instances[0].destroyed, 0, 'earlier models survive slow later initialization');
        release(); assert.equal(await result, null);
        assert.equal(timers.size, 2);
        for (const timer of timers.values()) assert.equal(timer.delay, 60000);
        await advance(59000);
        assert.equal(await translateWithPivot('en','de','hello'), 'fast');
        await advance(2000);
        assert.equal(instances[0].destroyed, 0, 'request refreshes warmed engine idle budget');
        assert.equal(instances[1].destroyed, 1, 'unused warmed engine is reclaimed');
        instances[0].busy = true;
        await advance(59000);
        assert.equal(instances[0].destroyed, 0, 'busy engine stays alive');
        instances[0].busy = false;
        await advance(60000);
        assert.equal(instances[0].destroyed, 1);
        assert.equal(getEngineRuntimes().length, 0, 'warmup never pins idle models forever');
      } else if (scenario.startsWith('shutdown')) {
        await entered;
        const closing = cleanupAllEngines();
        release(); await closing;
        assert.match(String(await result), /interrupted by shutdown/);
        assert.equal(prepares.length, 1, 'shutdown stops warming additional pairs');
        assert.equal(instances.length, scenario === 'shutdown-prepare' ? 0 : 1, 'shutdown during preparation cannot create an engine');
        assert.equal(getEngineRuntimes().length, 0);
        assert.ok(instances.every(instance => instance.destroyed === 1));
        assert.equal(timers.size, 0);
      } else if (scenario.startsWith('late-')) {
        await entered;
        setConfig(scenario === 'late-mode' ? {fast:false} : {workersPerLanguage:4});
        release(); assert.equal(await result, null);
        assert.equal(instances.length,4);
        assert.equal(instances[0].destroyed,1,'earlier prepared models also follow the final configuration');
        assert.equal(instances[1].destroyed,1);
        assert.ok(prepares.slice(2).every(item=>item.fast === (scenario !== 'late-mode')));
        assert.ok(configs.slice(2).every(item=>item['max-concurrent-runs'] === (scenario === 'late-workers' ? 4 : 2)));
        assert.equal(await translateWithPivot('en','de','hello'),scenario === 'late-mode' ? 'standard' : 'fast');
        assert.equal(instances.length,4);
      } else if (['concurrent','mode','workers'].includes(scenario)) {
        await entered;
        if (scenario === 'mode') setConfig({fast:false});
        if (scenario === 'workers') setConfig({workersPerLanguage:4});
        const request = translateWithPivot('en','de','hello');
        release(); assert.equal(await result, null);
        assert.equal(await request, scenario === 'mode' ? 'standard' : 'fast');
        assert.equal(instances.length, scenario === 'concurrent' ? 2 : 3);
        if (scenario !== 'concurrent') assert.equal(instances[0].destroyed, 1);
        assert.equal(configs.at(-1)['max-concurrent-runs'], scenario === 'workers' ? 4 : 2);
        const count = instances.length;
        await translateWithPivot('en','de','again');
        assert.equal(instances.length, count, 'request reuses startup worker');
      } else if (scenario === 'failure' || scenario === 'manifest-failure') {
        assert.equal(await result, null, 'a failed direction does not block other languages');
        assert.equal(warnings.length, 1, 'partial failure is reported');
        assert.match(warnings[0], /en -> de/);
        assert.match(warnings[0], scenario === 'failure' ? /invalid local graph/ : /JSON/);
        assert.deepEqual(getEngineRuntimes().map(({from,to})=>from+'_'+to), ['fr_en'], 'only successful directions are ready');
        if (scenario === 'failure') assert.equal(instances[0].destroyed, 1, 'failed initialization is cleaned');
        assert.equal(await translateWithPivot('fr','en','bonjour'), 'fast', 'later directions remain usable');
        assert.equal(timers.size, 1);
      } else {
        assert.equal(await result, null);
        assert.equal(instances.length, scenario === 'empty' ? 0 : 2);
        for (const runtime of getEngineRuntimes()) assert.equal(runtime.runtime, 'native-async');
        if (scenario === 'disabled') assert.ok(prepares.every(item => !item.fast), 'startup respects fast=false');
      }
      assert.ok(prepares.every(item => item.localOnly === true), 'startup cannot download even on conversion repair');
      assert.ok(prepares.every(item => pairs.includes(item.pair)), 'raw-only and unknown directories stay lazy');
      if (!['failure','manifest-failure'].includes(scenario)) assert.equal(warnings.length, 0, 'abort is propagated rather than reported as a model warning');
    } finally {
      release(); await result;
      await cleanupAllEngines();
      assert.equal(timers.size, 0);
      assert.ok(instances.every(instance => instance.destroyed === 1));
      mock.restore();
      await rm(root, {recursive:true, force:true});
    }
  `), 10000,
);

test.each(['ready', 'scan-failure', 'listen-failure', 'signal', 'signal-records', 'abort', 'abort-records', 'pre-abort', 'records-failure', 'cleanup-failure', 'running-abort'])(
  'HTTP startup reports readiness only after warmup and releases failures: %s', async scenario => isolated(`
    import { mock } from 'bun:test';
    import assert from 'node:assert/strict';
    import { mkdir, mkdtemp, rm } from 'node:fs/promises';
    import { createServer } from 'node:net';
    import { once, getEventListeners } from 'node:events';
    import path from 'node:path';
    import { setConfig } from './src/config/index.ts';
    const scenario = ${JSON.stringify(scenario)};
    await mkdir('.tmp', {recursive:true});
    const root = await mkdtemp(path.resolve('.tmp/http-warmup-'));
    const probe = createServer(); probe.listen(0,'127.0.0.1'); await once(probe, 'listening');
    const port = probe.address().port;
    if (scenario !== 'listen-failure') await new Promise(resolve => probe.close(resolve));
    setConfig({homeDir:root, configDir:root, modelDir:root, host:'127.0.0.1', port:String(port), checkUpdate:false, fast:false});
    let enter, release, cleanups = 0, records = 0, warmups = 0;
    const entered = new Promise(resolve => enter = resolve);
    const gate = new Promise(resolve => release = resolve);
    const logs = [];
    mock.module('./src/logger/index.ts', () => ({info:()=>{}, important:message=>logs.push(message), error:()=>{}}));
    mock.module('./src/models/index.ts', () => ({initRecords:async()=>{
      records++;
      if (scenario.endsWith('-records') || scenario === 'records-failure') { enter(); await gate; }
      if (scenario === 'records-failure') throw new Error('broken model registry');
    }}));
    mock.module('./src/services/index.ts', () => ({
      warmupEngines:async()=>{warmups++; enter(); await gate; if (['scan-failure','cleanup-failure'].includes(scenario)) throw new Error('cannot scan model directory');},
      cleanupAllEngines:async()=>{cleanups++; release(); if (scenario === 'cleanup-failure') throw new Error('cleanup failed');},
      getEngineRuntimes:()=>[],
    }));
    mock.module('./src/generated/routes.ts', () => ({RegisterRoutes:app=>app.get('/health', (_,res)=>res.json({status:'ok'}))}));
    mock.module('./src/middleware/ui.ts', () => ({uiStatic:(_req,_res,next)=>next()}));
    mock.module('./src/middleware/swagger.ts', () => ({swaggerStatic:(_req,_res,next)=>next()}));
    const {startServer} = await import('./src/server/index.ts');
    const oldExit = process.exit;
    let exitCode;
    const handlesSignals = scenario.startsWith('signal') || scenario === 'cleanup-failure';
    if (handlesSignals) process.exit = code => { exitCode = code; };
    const before = ['SIGINT','SIGTERM'].map(name=>process.listenerCount(name));
    const controller = new AbortController();
    const reason = new Error('cancel startup');
    if (scenario === 'pre-abort') controller.abort(reason);
    let running;
    const starting = startServer({handleSignals:handlesSignals, signal:controller.signal}).then(value=>{running=value;return null;},error=>error);
    try {
      if (scenario !== 'pre-abort') await entered;
      assert.equal(running, undefined);
      assert.equal(logs.length, 0, 'no running announcement before warmup');
      const base = 'http://127.0.0.1:'+port;
      if (scenario !== 'listen-failure') await assert.rejects(fetch(base+'/health', {signal:AbortSignal.timeout(1000)}));
      if (scenario.startsWith('signal')) process.emit('SIGTERM');
      else if (scenario === 'abort' || scenario === 'abort-records') controller.abort(reason);
      else release();
      const error = await starting;
      if (scenario === 'ready' || scenario === 'running-abort') {
        assert.equal(error, null);
        assert.equal(running.server.listening, true, 'startServer only resolves once bound');
        assert.deepEqual(await (await fetch(base+'/health')).json(), {status:'ok'});
        assert.ok(logs.some(message=>message.includes('is running!')));
        if (scenario === 'running-abort') controller.abort(reason);
        await Promise.all([running.stop(),running.stop()]);
        assert.equal(running.server.listening, false);
      } else {
        assert.ok(error instanceof Error);
        if (scenario === 'scan-failure') assert.match(error.message,/cannot scan model directory/);
        if (scenario === 'records-failure') assert.match(error.message,/broken model registry/);
        if (scenario === 'cleanup-failure') assert.match(error.message,/cleanup failed/);
        if (['abort','abort-records','pre-abort'].includes(scenario)) assert.equal(error, reason);
        if (scenario === 'listen-failure') assert.equal(error.code,'EADDRINUSE');
        if (scenario.startsWith('signal')) {
          await new Promise(resolve=>setImmediate(resolve));
          assert.equal(exitCode,0);
        }
        assert.equal(logs.length, 0, 'failure is never advertised as ready');
      }
      if (scenario.endsWith('-records') || scenario === 'records-failure' || scenario === 'pre-abort') assert.equal(warmups, 0, 'early cancellation or failure never starts warmup');
      if (scenario === 'pre-abort') assert.equal(records, 0);
      assert.equal(cleanups, 1);
      assert.deepEqual(['SIGINT','SIGTERM'].map(name=>process.listenerCount(name)), before, 'process signal listeners are removed');
      assert.equal(getEventListeners(controller.signal, 'abort').length, 0, 'external abort listeners are removed');
    } finally {
      release(); await starting;
      if (running) await running.stop();
      if (probe.listening) await new Promise(resolve=>probe.close(resolve));
      process.exit = oldExit;
      await rm(root, {recursive:true, force:true});
    }
  `), 10000,
);

test('local startup model repair cannot download and a model without lex retains standard conversion', async () => isolated(`
  import { mock } from 'bun:test';
  import assert from 'node:assert/strict';
  import { mkdir, mkdtemp, writeFile, rm } from 'node:fs/promises';
  import path from 'node:path';
  import { setConfig } from './src/config/index.ts';
  await mkdir('.tmp', {recursive:true});
  const root = await mkdtemp(path.resolve('.tmp/warmup-local-model-'));
  let downloads = 0, expectsLex = false;
  mock.module('./src/core/factory.ts', () => ({createDownloader:()=>({download:async()=>{downloads++;throw new Error('unexpected model download');}})}));
  mock.module('./src/converter/cache.ts', () => ({ensureConverted:async (_root,_pair,source,repair,fast)=>{
    assert.equal(fast,true);
    assert.equal(source.files.lex,expectsLex ? path.join(root,'en_de/lex') : undefined);
    return repair();
  }}));
  const records = ['model','vocab'].map(fileType=>({fileType,version:'1',sourceLanguage:'en',targetLanguage:'de',attachment:{filename:fileType+'.zst',location:'unused'}}));
  await writeFile(path.join(root,'records.json'), JSON.stringify({data:records}));
  setConfig({configDir:root,modelDir:root,fast:true,enableOfflineMode:true,logConsole:false});
  const {initRecords,prepareModel} = await import('./src/models/records.ts');
  await initRecords();
  setConfig({enableOfflineMode:false});
  try {
    await assert.rejects(prepareModel('de','en',undefined,true,true), /ENOENT/);
    assert.equal(downloads,0,'missing source is reported without downloading');
    await mkdir(path.join(root,'en_de'));
    for (const name of ['model','vocab']) await writeFile(path.join(root,'en_de',name),'local');
    const files = await prepareModel('de','en',undefined,true,true);
    assert.equal(files.lex,undefined);
    assert.equal(files.model,path.join(root,'en_de/model'));
    assert.equal(downloads,0);
    expectsLex = true;
    await writeFile(path.join(root,'records.json'),JSON.stringify({data:[...records,{...records[0],fileType:'lex',attachment:{filename:'lex.zst',location:'unused'}}]}));
    setConfig({enableOfflineMode:true}); await initRecords(); setConfig({enableOfflineMode:false});
    await assert.rejects(prepareModel('de','en',undefined,true,true), /ENOENT.*lex/);
    assert.equal(downloads,0,'a missing registered lex file fails honestly without a download');
  } finally {await rm(root,{recursive:true,force:true});}
`));
