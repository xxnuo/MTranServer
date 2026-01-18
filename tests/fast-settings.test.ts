import { expect, test } from 'bun:test';
import { fileURLToPath } from 'node:url';

test('settings exposes and updates canonical fast without losing explicit alias choices', async () => {
  const script = `
    import { mock } from 'bun:test';
    import assert from 'node:assert/strict';
    import { mkdir, mkdtemp, rm } from 'node:fs/promises';
    import path from 'node:path';
    import { once } from 'node:events';
    import { setConfig, getConfig } from './src/config/index.ts';
    mock.module('./src/models/index.ts', () => ({ initRecords:async()=>{} }));
    mock.module('./src/services/index.ts', () => ({ cleanupAllEngines:async()=>{}, getEngineRuntimes:()=>[] }));
    mock.module('./src/generated/routes.ts', () => ({ RegisterRoutes:()=>{} }));
    mock.module('./src/middleware/ui.ts', () => ({ uiStatic:(_req,_res,next)=>next() }));
    mock.module('./src/middleware/swagger.ts', () => ({ swaggerStatic:(_req,_res,next)=>next() }));
    await mkdir('.tmp',{recursive:true});
    const root = await mkdtemp(path.resolve('.tmp/fast-settings-'));
    setConfig({homeDir:root, configDir:root, modelDir:root, host:'127.0.0.1', port:'0', checkUpdate:false, fast:false, cacheSize:0});
    const { startServer } = await import('./src/server/index.ts');
    const { server, stop } = await startServer({handleSignals:false});
    try {
      if (!server.listening) await once(server, 'listening');
      const base = 'http://127.0.0.1:' + server.address().port;
      const settings = async () => (await (await fetch(base+'/ui/api/settings')).json()).config.server;
      assert.equal((await settings()).fast, false);
      assert.equal((await settings()).cacheSize, 0);
      for (const [patch, expected] of [
        [{fast:true},true], [{lexicalShortlist:false},false],
        [{fast:true,lexicalShortlist:false},true], [{fast:false,lexicalShortlist:true},false],
        [{fast:true},true], [{cacheSize:0},true],
      ]) {
        const response = await fetch(base+'/ui/api/settings/apply', {method:'POST', headers:{'content-type':'application/json'}, body:JSON.stringify({server:patch})});
        assert.equal(response.status,200);
        assert.equal((await response.json()).config.server.fast,expected);
        assert.equal((await settings()).fast,expected);
        assert.equal(getConfig().lexicalShortlist,expected);
      }
      globalThis.mtranDesktopControl = {getConfig:async()=>({config:{server:{fast:true,cacheSize:0}}})};
      assert.equal((await settings()).fast,true);
    } finally { delete globalThis.mtranDesktopControl; await stop(); await rm(root,{recursive:true,force:true}); }
  `;
  const child = Bun.spawn([process.execPath, '--eval', script], { cwd: fileURLToPath(new URL('..', import.meta.url)), stdout: 'pipe', stderr: 'pipe' });
  const [, stderr, code] = await Promise.all([new Response(child.stdout).text(), new Response(child.stderr).text(), child.exited]);
  expect(code, stderr).toBe(0);
});

test('desktop configuration persists canonical fast and passes it to the server environment', async () => {
  const script = `
    import { mock, spyOn } from 'bun:test';
    import assert from 'node:assert/strict';
    import fs from 'node:fs/promises';
    let saved = '';
    spyOn(fs,'mkdir').mockResolvedValue(undefined);
    spyOn(fs,'writeFile').mockImplementation(async (_file,data)=>{saved=String(data);});
    spyOn(fs,'unlink').mockResolvedValue(undefined);
    const desktop = await import('./desktop/config.js');
    assert.equal(desktop.getDefaultDesktopConfig().server.fast,false);
    for (const [input,expected] of [[{fast:true},true],[{lexicalShortlist:true},true],[{fast:false,lexicalShortlist:true},false],[{fast:true,lexicalShortlist:false},true]]) {
      const config = await desktop.saveDesktopConfig({server:input});
      assert.equal(config.server.fast,expected);
      if (expected) assert.match(saved,/fast: true/);
      spyOn(fs,'readFile').mockResolvedValue(expected ? saved : '{}');
      assert.equal((await desktop.loadDesktopConfig()).server.fast,expected);
    }
    let effective;
    mock.module('./dist/desktop.js',()=>({resetConfig(){},startServer:async()=>{
      effective=process.env.MT_FAST;
      return {server:{},stop:async()=>{}};
    }}));
    const {startServerWithConfig,stopServerInstance}=await import('./desktop/server.js');
    process.argv.push('--fast','--no-lexical-shortlist');
    process.env.MT_LEXICAL_SHORTLIST='true';
    await startServerWithConfig({...desktop.getDefaultDesktopConfig().server,fast:false});
    assert.equal(effective,'false');
    assert.ok(!process.argv.includes('--fast'));
    assert.ok(!process.argv.includes('--no-lexical-shortlist'));
    await stopServerInstance();
  `;
  const child = Bun.spawn([process.execPath, '--eval', script], { cwd: fileURLToPath(new URL('..', import.meta.url)), stdout: 'pipe', stderr: 'pipe' });
  const [, stderr, code] = await Promise.all([new Response(child.stdout).text(), new Response(child.stderr).text(), child.exited]);
  expect(code, stderr).toBe(0);
});

test('desktop fast-only HTTP apply preserves partial configuration and responds without restarting its own server', async () => {
  const script = `
    import { mock, spyOn } from 'bun:test';
    import assert from 'node:assert/strict';
    import fs from 'node:fs/promises';
    import path from 'node:path';
    import YAML from 'yaml';
    import { once } from 'node:events';
    import { getConfig, resetConfig, setConfig } from './src/config/index.ts';
    await fs.mkdir('.tmp',{recursive:true});
    const root = await fs.mkdtemp(path.resolve('.tmp/desktop-http-fast-'));
    const original = {locale:'ja',server:{host:'127.0.0.1',port:0,modelDir:root,configDir:root,logDir:root,apiToken:'keep-token',cacheSize:0,checkUpdate:false,fast:false}};
    let saved = YAML.stringify(original), starts = 0, stops = 0, active;
    const readFile = fs.readFile.bind(fs), writeFile = fs.writeFile.bind(fs), mkdir = fs.mkdir.bind(fs), unlink = fs.unlink.bind(fs);
    spyOn(fs,'readFile').mockImplementation((file,...args)=>String(file).endsWith('desktop.yml') ? Promise.resolve(saved) : readFile(file,...args));
    spyOn(fs,'writeFile').mockImplementation((file,data,...args)=>{
      if (String(file).endsWith('desktop.yml')) { saved=String(data); return Promise.resolve(); }
      return writeFile(file,data,...args);
    });
    spyOn(fs,'mkdir').mockImplementation((file,...args)=>String(file).startsWith(root) ? mkdir(file,...args) : Promise.resolve());
    spyOn(fs,'unlink').mockImplementation((file,...args)=>String(file).endsWith('desktop.yml') ? Promise.resolve() : unlink(file,...args));
    mock.module('electron',()=>({
      app:{on(){},getVersion:()=> '5.1.1',getLocale:()=> 'ja',getLoginItemSettings:()=>({openAtLogin:false}),quit(){throw new Error('unexpected quit');}},
      dialog:{showMessageBox:async()=>({response:0})},
      nativeImage:{createFromPath:()=>({addRepresentation(){},setTemplateImage(){},toPNG(){return Buffer.alloc(0);}})},
      shell:{},Menu:{setApplicationMenu(){}},screen:{getPrimaryDisplay:()=>({scaleFactor:1})}
    }));
    mock.module('./desktop/tray.js',()=>({createTray:()=>({}),updateTrayMenu(){}}));
    mock.module('./desktop/ipc.js',()=>({registerIpcHandlers(){}}));
    mock.module('./desktop/ports.js',()=>({isPortAvailable:async()=>true,getFreePort:async()=>0}));
    mock.module('./src/models/index.ts',()=>({initRecords:async()=>{}}));
    mock.module('./src/services/index.ts',()=>({cleanupAllEngines:async()=>{},getEngineRuntimes:()=>[]}));
    mock.module('./src/generated/routes.ts',()=>({RegisterRoutes(){}}));
    mock.module('./src/middleware/ui.ts',()=>({uiStatic:(_req,_res,next)=>next()}));
    mock.module('./src/middleware/swagger.ts',()=>({swaggerStatic:(_req,_res,next)=>next()}));
    const {startServer}=await import('./src/server/index.ts');
    mock.module('./dist/desktop.js',()=>({resetConfig,setConfig,startServer:async options=>{
      starts++;
      active=await startServer(options);
      return {server:active.server,stop:async()=>{stops++;await active.stop();}};
    }}));
    const originalFetch = globalThis.fetch;
    globalThis.fetch = (url,...args)=>String(url).startsWith('https://github.com/') ? Promise.resolve(new Response('',{status:503})) : originalFetch(url,...args);
    const {startDesktop}=await import('./desktop/app.js');
    const {stopServerInstance}=await import('./desktop/server.js');
    try {
      await startDesktop();
      assert.ok(globalThis.mtranDesktopControl);
      if (!active.server.listening) await once(active.server,'listening');
      const base='http://127.0.0.1:'+active.server.address().port;
      for (const [patch,expected] of [[{fast:true},true],[{lexicalShortlist:false},false],[{lexicalShortlist:true},true],[{fast:false,lexicalShortlist:true},false]]) {
        // Real route -> real desktop control -> real save/normalize -> hot setConfig.
        const response=await originalFetch(base+'/ui/api/settings/apply',{
          method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({server:patch}),signal:AbortSignal.timeout(2000)
        });
        assert.equal(response.status,200);
        const payload=await response.json();
        assert.equal(payload.config.locale,'ja');
        assert.equal(payload.config.server.fast,expected);
        for (const key of ['port','modelDir','configDir','logDir','apiToken','cacheSize']) assert.equal(payload.config.server[key],original.server[key],key);
        assert.equal(getConfig().fast,expected);
        assert.equal(getConfig().lexicalShortlist,expected);
        assert.equal(process.env.MT_FAST,String(expected));
        assert.equal(starts,1);
        assert.equal(stops,0);
        assert.equal(active.server.listening,true);
        const current=await (await originalFetch(base+'/ui/api/settings')).json();
        assert.equal(current.config.server.fast,expected);
        assert.equal(YAML.parse(saved).server.fast ?? false,expected);
      }
    } finally {
      delete globalThis.mtranDesktopControl;
      globalThis.fetch=originalFetch;
      await stopServerInstance();
      mock.restore();
      await fs.rm(root,{recursive:true,force:true});
    }
  `;
  const child = Bun.spawn([process.execPath, '--eval', script], { cwd: fileURLToPath(new URL('..', import.meta.url)), stdout: 'pipe', stderr: 'pipe' });
  const [, stderr, code] = await Promise.all([new Response(child.stdout).text(), new Response(child.stderr).text(), child.exited]);
  expect(code, stderr).toBe(0);
}, 15000);
