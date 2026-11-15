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

test.each(['records-exit', 'records-restart', 'prepare-exit', 'prepare-restart', 'init-exit', 'init-restart'])(
  'desktop cancels and drains startup before exit or restart: %s', async scenario => isolated(`
    import { mock } from 'bun:test';
    import assert from 'node:assert/strict';
    import { mkdir, mkdtemp, writeFile, rm } from 'node:fs/promises';
    import { getEventListeners } from 'node:events';
    import path from 'node:path';
    import { resetConfig, setConfig } from './src/config/index.ts';
    const scenario = ${JSON.stringify(scenario)};
    await mkdir('.tmp', {recursive:true});
    const root = await mkdtemp(path.resolve('.tmp/desktop-startup-'));
    await mkdir(path.join(root,'en_de'));
    await writeFile(path.join(root,'en_de/manifest.json'), JSON.stringify({identity:'fast'}));
    let enter, release, starts = 0, records = 0, prepares = 0, live = 0;
    const entered = new Promise(resolve=>enter=resolve);
    const gate = new Promise(resolve=>release=resolve);
    const instances = [], signals = [];
    const before = ['SIGINT','SIGTERM'].map(name=>process.listenerCount(name));
    mock.module('./src/logger/index.ts',()=>({info(){},debug(){},warn(){},error(){},important(){}}));
    mock.module('./src/core/engine.ts',()=>({TranslationEngine:class {
      busy = false; destroyed = 0; backend = {};
      constructor() { instances.push(this); assert.equal(++live,1,'only one set of engines may be alive'); }
      async init(model) {
        this.identity = model.identity;
        if (scenario.startsWith('init-') && instances.length === 1) { enter(); await gate; }
      }
      async translateAsync() { assert.equal(this.destroyed,0); return this.identity; }
      async destroy() { assert.equal(++this.destroyed,1); live--; }
    }}));
    mock.module('./src/models/index.ts',()=>({
      initRecords:async()=>{ if (++records === 1 && scenario.startsWith('records-')) { enter(); await gate; } },
      hasLanguagePair:()=>true,
      prepareModel:async(_to,_from,_version,fast,localOnly)=>{
        assert.equal(localOnly,true);
        if (++prepares === 1 && scenario.startsWith('prepare-')) { enter(); await gate; }
        const identity = fast ? 'fast' : 'standard';
        await writeFile(path.join(root,'en_de/manifest.json'),JSON.stringify({identity}));
        return {identity};
      },
    }));
    mock.module('./src/services/detector.ts',()=>({detectLanguage:async()=> 'en',detectMultipleLanguages:async()=>[]}));
    mock.module('./src/utils/cache.ts',()=>({readCache:()=>null,writeCache(){}}));
    mock.module('./src/generated/routes.ts',()=>({RegisterRoutes:app=>app.get('/health',(_,res)=>res.json({status:'ok'}))}));
    mock.module('./src/middleware/ui.ts',()=>({uiStatic:(_req,_res,next)=>next()}));
    mock.module('./src/middleware/swagger.ts',()=>({swaggerStatic:(_req,_res,next)=>next()}));
    const {startServer} = await import('./src/server/index.ts');
    mock.module('./dist/desktop.js',()=>({resetConfig,setConfig,startServer:options=>{
      starts++;
      assert.equal(options.handleSignals,false);
      signals.push(options.signal);
      return startServer(options);
    }}));
    const {getDefaultDesktopConfig} = await import('./desktop/config.js');
    const {startServerWithConfig,stopServerInstance,getServerStatus} = await import('./desktop/server.js');
    const {getEngineRuntimes,translateWithPivot} = await import('./src/services/engine.ts');
    const config = {...getDefaultDesktopConfig().server, host:'127.0.0.1',port:0,modelDir:root,configDir:root,logDir:root,checkUpdate:false,logConsole:false,logToFile:false,fast:true};
    let closed = false;
    const starting = startServerWithConfig(config).then(()=>null,error=>error);
    let restarting;
    try {
      await entered;
      assert.equal(getServerStatus(),'starting');
      const closing = Promise.all([stopServerInstance(),stopServerInstance()]).then(()=>{closed=true;});
      if (scenario.endsWith('-restart')) restarting = startServerWithConfig({...config,fast:false});
      assert.equal(signals[0].aborted,true,'stop immediately cancels the pending start');
      for (let i=0;i<5;i++) await new Promise(resolve=>setImmediate(resolve));
      assert.equal(closed,false,'stop waits for initialization and cleanup');
      assert.equal(starts,1,'restart waits for the old startup to settle');
      assert.equal(process.env.MT_FAST,'true','queued config cannot change an active startup');
      release();
      assert.match(String(await starting),/interrupted by shutdown/);
      await closing;
      if (restarting) {
        const server = await restarting;
        assert.equal(starts,2);
        assert.equal(server.listening,true);
        assert.equal(getServerStatus(),'running');
        assert.equal(live,1);
        assert.equal(await translateWithPivot('en','de','hello'),'standard');
        const response = await fetch('http://127.0.0.1:'+server.address().port+'/health');
        assert.deepEqual(await response.json(),{status:'ok'});
      } else {
        assert.equal(getServerStatus(),'stopped');
        assert.equal(live,0);
        assert.equal(getEngineRuntimes().length,0);
      }
      assert.equal(instances.length,(scenario.startsWith('init-') ? 1 : 0)+(restarting ? 1 : 0));
    } finally {
      release(); await starting;
      if (restarting) await restarting;
      await stopServerInstance();
      assert.equal(live,0);
      assert.equal(getServerStatus(),'stopped');
      assert.deepEqual(['SIGINT','SIGTERM'].map(name=>process.listenerCount(name)),before);
      for (const signal of signals) assert.equal(getEventListeners(signal,'abort').length,0,'abort listeners do not survive stop');
      await rm(root,{recursive:true,force:true});
    }
  `), 10000,
);

test('desktop serializes queued starts and stops, and recovers after a failed start', async () => isolated(`
  import { mock } from 'bun:test';
  import assert from 'node:assert/strict';
  let starts = 0, stops = 0, live = 0, fail = true;
  mock.module('./dist/desktop.js',()=>({resetConfig(){},startServer:async({signal})=>{
    signal.throwIfAborted(); starts++;
    if (fail) throw new Error('startup failed');
    assert.equal(++live,1);
    return {server:{id:starts},stop:async()=>{stops++;live--;}};
  }}));
  const {startServerWithConfig,stopServerInstance,getServerStatus} = await import('./desktop/server.js');
  const {getDefaultDesktopConfig} = await import('./desktop/config.js');
  const config = getDefaultDesktopConfig().server;
  await assert.rejects(startServerWithConfig(config),/startup failed/);
  assert.equal(getServerStatus(),'stopped');
  fail = false;
  const queued = [startServerWithConfig(config),startServerWithConfig(config)].map(p=>p.then(()=>null,error=>error));
  await stopServerInstance();
  for (const result of await Promise.all(queued)) assert.match(String(result),/interrupted by shutdown/);
  assert.equal(starts,1,'cancelled queued starts never enter the server module');
  const servers = await Promise.all([startServerWithConfig(config),startServerWithConfig(config)]);
  assert.deepEqual(servers.map(server=>server.id),[2,3]);
  assert.equal(live,1);
  await Promise.all([stopServerInstance(),stopServerInstance()]);
  assert.equal(live,0);
  assert.equal(stops,2,'each running server is stopped once');
  assert.equal(getServerStatus(),'stopped');
`));
