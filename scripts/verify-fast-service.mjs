#!/usr/bin/env node
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { createReadStream } from 'node:fs';
import { access, copyFile, cp, mkdir, mkdtemp, readFile, readdir, symlink, writeFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { createServer } from 'node:net';
import { cpus, release } from 'node:os';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { inspect, parseArgs } from 'node:util';

const root = fileURLToPath(new URL('../', import.meta.url));
const pairs = ['en_zh-Hans', 'zh-Hans_en'];
const samples = [
  { from: 'en', to: 'zh-Hans', short: 'The library is open today.',
    paragraph: 'The library is open today. We can read books in the afternoon. Please bring your notebook.',
    html: '<p data-test="keep">The library is open today. <strong>Welcome to our city.</strong></p>' },
  { from: 'zh-Hans', to: 'en', short: '今天图书馆开门。',
    paragraph: '今天图书馆开门。我们下午可以去看书。请带上你的笔记本。',
    html: '<p data-test="keep">今天图书馆开门。<strong>欢迎来到我们的城市。</strong></p>' },
];

// Observe accepted HTTP requests and invoke the product's signal handler on Windows too.
export const servicePreload = `
import { Server } from 'node:http';
process.send?.({ host: { execPath: process.execPath, node: process.versions.node,
  electron: process.versions.electron ?? null, napi: process.versions.napi,
  platform: process.platform, arch: process.arch, native_ssru_env: process.env.MT_NATIVE_SSRU ?? null } });
const dlopen = process.dlopen;
process.dlopen = function(module, filename, ...args) {
  const value = dlopen.call(this, module, filename, ...args);
  if (/onnx-async-[^/\\\\]+\\.node$/.test(filename)) process.send?.({ addon_loaded: {
    filename, ssruVersion: module.exports.ssruVersion, ssruBackend: module.exports.ssruBackend ?? null } });
  return value;
};
const emit = Server.prototype.emit;
const active = new Set();
Server.prototype.emit = function(event, ...args) {
  const id = event === 'request' && args[0].headers['x-verify-drain'];
  if (id) {
    active.add(id);
    args[1].once('finish', () => active.delete(id));
    args[1].once('close', () => active.delete(id));
    process.send?.({ accepted: id });
  }
  return emit.call(this, event, ...args);
};
process.on('message', message => {
  if (message === 'verify-fast-service:stop') {
    process.send?.({ stopping: true, active: [...active] });
    process.emit('SIGTERM');
  }
});
`;

export function options(args = process.argv.slice(2)) {
  const { values, positionals } = parseArgs({ args, allowPositionals: true, options: {
    dist: { type: 'string' }, runtime: { type: 'string' }, models: { type: 'string' }, 'work-dir': { type: 'string' },
    report: { type: 'string' }, 'timeout-ms': { type: 'string', default: '180000' },
    help: { type: 'boolean', short: 'h' },
  } });
  assert(positionals.length <= 1 && !(positionals.length && values.dist), 'Use either a main.js positional path or --dist DIR');
  const timeout = Number(values['timeout-ms']);
  assert(Number.isSafeInteger(timeout) && timeout > 0 && timeout <= 2147483647, 'Invalid --timeout-ms');
  const work = path.resolve(values['work-dir'] ?? path.join(root, '.tmp/fast-service'));
  return { help: values.help, entry: path.resolve(positionals[0] ?? path.join(values.dist ?? path.join(root, 'dist'), 'main.js')),
    runtime: path.resolve(values.runtime ?? process.execPath),
    models: path.resolve(values.models ?? process.env.SSRU_VERIFY_DIR ?? path.join(root, '.tmp/ssru-platform')),
    work, report: path.resolve(values.report ?? path.join(work, 'http-result.json')), timeout };
}

async function hashFile(filename) {
  const hash = createHash('sha256');
  for await (const bytes of createReadStream(filename)) hash.update(bytes);
  return hash.digest('hex');
}
const json = async filename => JSON.parse(await readFile(filename, 'utf8'));
const save = async (filename, value) => {
  await mkdir(path.dirname(filename), { recursive: true });
  await writeFile(filename, JSON.stringify(value, null, 2) + '\n');
};
async function within(promise, timeout, message) {
  let timer;
  try {
    return await Promise.race([promise, new Promise((_, reject) => {
      timer = setTimeout(() => reject(new Error(message)), timeout);
    })]);
  } finally { clearTimeout(timer); }
}

async function prepareModels(source, directory, registry, fixtures) {
  const models = path.join(directory, 'models');
  const evidence = [];
  for (const fixture of fixtures) {
    const baseline = registry.manifests.find(row => row.pair === fixture.pair);
    assert(baseline, `Missing baseline manifest: ${fixture.pair}`);
    const target = path.join(models, fixture.pair);
    await mkdir(target, { recursive: true });
    for (const file of fixture.files) {
      assert.equal(path.basename(file.path), file.path, 'Invalid source filename');
      const input = path.join(source, fixture.pair, file.path);
      const sha256 = await hashFile(input);
      assert.equal(sha256, file.sha256, `Source hash mismatch: ${input}`);
      const record = registry.data.find(row => `${row.sourceLanguage}_${row.targetLanguage}` === fixture.pair
        && row.attachment.filename.replace(/\.zst$/, '') === file.path);
      assert.equal(record?.decompressedHash, sha256, `Pinned registry mismatch: ${input}`);
      assert.equal(record.attachment.hash, file.downloadSha256);
      await copyFile(input, path.join(target, file.path));
    }
    await copyFile(path.join(source, fixture.pair, 'fast.onnx'), path.join(target, 'model.onnx'));
    const fastHash = await hashFile(path.join(target, 'model.onnx'));
    assert.equal(fastHash, baseline.files.find(file => file.role === 'inference-model')?.sha256,
      `Fast graph differs from fixed product baseline: ${fixture.pair}`);
    const { pair, ...manifest } = baseline;
    manifest.sourceModelSha256 = fixture.files.find(file => file.role === 'source-model').sha256;
    manifest.records = registry.data.filter(row => `${row.sourceLanguage}_${row.targetLanguage}` === pair)
      .map(row => ({ ...row, url: `https://firefox-settings-attachments.cdn.mozilla.net/${row.attachment.location}` }))
      .sort((a, b) => JSON.stringify(a).localeCompare(JSON.stringify(b)));
    await save(path.join(target, 'manifest.json'), manifest);
    evidence.push({ pair, identity: manifest.identity, fast_sha256: fastHash,
      ssru_sha256: await hashFile(path.join(source, pair, 'ssru.onnx')),
      source_sha256: manifest.sourceModelSha256, lex_sha256: manifest.lexSha256 });
  }
  return { models, evidence };
}

export function assertSettings(settings, runtime) {
  const config = settings.config?.server;
  assert.equal(settings.status, 'running');
  assert.equal(config?.fast, true);
  assert.equal(config.cacheSize, 0);
  assert.equal(config.enableOfflineMode, true);
  assert.equal(config.checkUpdate, false);
  assert.equal(settings.engines?.length, 2, 'Both engines must already be warm at the first HTTP response');
  for (const pair of pairs) {
    const engine = settings.engines.find(row => `${row.from}_${row.to}` === pair);
    assert(engine, `Engine was not warmed: ${pair}`);
    assert.equal(engine.provider, 'cpu');
    assert.equal(engine.runtime, runtime, `Unexpected runtime: ${pair}`);
    if (runtime === undefined) assert.match(engine.fallbackReason ?? '', /addon unavailable/i);
    else assert.equal(engine.fallbackReason, undefined, `Unexpected runtime fallback: ${pair}`);
  }
}

export function assertTranslation(result, sample, text, html = false) {
  assert.equal(typeof result, 'string');
  assert(result.trim().length > 0 && result !== text, 'Translation must change the input');
  const plain = html ? result.replace(/<[^>]*>/g, '') : result;
  if (sample.to === 'zh-Hans') assert.match(plain, /\p{Script=Han}/u, 'Expected Chinese translation');
  else {
    assert.match(plain, /[A-Za-z]{2,}/, 'Expected English translation');
    assert.doesNotMatch(plain, /\p{Script=Han}/u, 'Chinese source leaked into English translation');
  }
  if (html) assert.deepEqual(result.match(/<[^>]*>/g), text.match(/<[^>]*>/g), 'HTML tags/attributes changed');
}

async function freePort() {
  const server = createServer();
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  const { port } = server.address();
  await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
  return port;
}

async function scenario(name, entry, runtime, context, result) {
  const directory = path.join(context.directory, name);
  const config = path.join(directory, 'config');
  await mkdir(config, { recursive: true });
  await save(path.join(config, 'records.json'), { data: context.registry.data });
  // HOME/USERPROFILE keep user server.json out of the product's config precedence.
  const env = Object.fromEntries(Object.entries(process.env).filter(([key]) =>
    !/^(MT_|MTRAN_|NODE_OPTIONS$|NODE_PATH$|ELECTRON_RUN_AS_NODE$|UV_THREADPOOL_SIZE$|HOME$|USERPROFILE$)/i.test(key)));
  Object.assign(env, { HOME: directory, USERPROFILE: directory, MT_CPU_ONLY: 'true', MT_WORKERS_PER_LANGUAGE: '2', UV_THREADPOOL_SIZE: '4' });
  const electron = /^electron(?:\.exe)?$/i.test(path.basename(context.runtime));
  if (electron) env.ELECTRON_RUN_AS_NODE = '1';
  if (name === 'disabled') env.MT_NATIVE_SSRU = '0';
  assert.equal(env.MT_NATIVE_SSRU, name === 'disabled' ? '0' : undefined);
  const port = await freePort();
  const base = `http://127.0.0.1:${port}`;
  const args = ['--import', 'data:text/javascript,' + encodeURIComponent(servicePreload), entry,
    '--host', '127.0.0.1', '--port', String(port), '--config-dir', config,
    '--model-dir', context.models, '--log-dir', path.join(directory, 'logs'),
    '--fast', '--offline', '--cache-size', '0', '--no-check-update', '--ui', '--log-console', '--log-level', 'info',
    '--worker-idle-timeout', '600'];
  Object.assign(result, { name, entry, native_ssru_env: env.MT_NATIVE_SSRU ?? null, expected_runtime: runtime ?? 'standard-ort',
    status: 'running', http: [], startup: { refused_connections: 0 }, log: path.join(directory, 'service.log') });
  const started = performance.now();
  const child = spawn(context.runtime, args, { cwd: directory, env, stdio: ['ignore', 'pipe', 'pipe', 'ipc'], windowsHide: true });
  result.addon_loads = [];
  child.on('message', message => {
    if (message.host) result.host = message.host;
    if (message.addon_loaded) result.addon_loads.push(message.addon_loaded);
  });
  let logs = '', exit, spawnError;
  const closed = new Promise(resolve => {
    child.on('error', error => { spawnError = error; });
    child.once('close', (code, signal) => { exit = { code, signal }; resolve(exit); });
  });
  for (const stream of [child.stdout, child.stderr]) stream.on('data', bytes => { logs += bytes.toString(); });
  const request = async (route, init = {}) => {
    const response = await fetch(base + route, { ...init, signal: AbortSignal.timeout(context.timeout), headers: {
      connection: 'close', ...init.headers,
    } });
    const body = await response.text();
    assert.equal(response.status, 200, `${route}: HTTP ${response.status}: ${body}`);
    return { response, body };
  };
  const translate = async (sample, text, kind, id) => {
    const start = performance.now();
    const { body } = await request('/translate', { method: 'POST', headers: {
      'content-type': 'application/json', ...(id ? { 'x-verify-drain': id } : {}),
    }, body: JSON.stringify({ from: sample.from, to: sample.to, text, html: kind === 'html' }) });
    const output = JSON.parse(body).result;
    assertTranslation(output, sample, text, kind === 'html');
    result.http.push({ kind, from: sample.from, to: sample.to, input: text, result: output, status: 200, elapsed_ms: performance.now() - start });
    return output;
  };
  const stop = () => new Promise((resolve, reject) => child.send('verify-fast-service:stop', error => error ? reject(error) : resolve()));
  try {
    let settings;
    while (true) {
      assert(!spawnError, String(spawnError));
      assert(!exit, `Service exited before ready: ${JSON.stringify(exit)}`);
      assert(performance.now() - started < context.timeout, 'Timed out waiting for HTTP readiness');
      let first;
      try { first = await request('/ui/api/settings'); }
      catch (error) {
        if (error.cause?.code !== 'ECONNREFUSED') throw error;
        result.startup.refused_connections++;
        await delay(25);
        continue;
      }
      settings = JSON.parse(first.body);
      assertSettings(settings, runtime);
      break;
    }
    result.startup.ready_ms = performance.now() - started;
    result.startup.settings = settings;
    assert(result.host, 'Missing runtime host metadata');
    assert.equal(result.host.native_ssru_env, env.MT_NATIVE_SSRU ?? null);
    assert.equal(result.host.platform, process.platform);
    assert.equal(result.host.arch, process.arch);
    if (electron) assert(result.host.electron, 'Expected a real Electron host');
    else assert.equal(result.host.electron, null, 'Unexpected Electron host');
    if (name === 'missing-addon') assert.equal(result.addon_loads.length, 0, 'Isolated host loaded a shared addon');
    else assert(result.addon_loads.some(addon => addon.ssruVersion === 1), 'Host did not load the SSRU N-API addon');
    const { response, body } = await request('/ui/');
    assert.match(response.headers.get('content-type') ?? '', /text\/html/);
    assert.match(body, /<html[\s>]/i);
    result.ui = { status: response.status, bytes: Buffer.byteLength(body) };
    for (const sample of samples) {
      await translate(sample, sample.short, 'short');
      if (name === 'default') {
        await translate(sample, sample.paragraph, 'paragraph');
        await translate(sample, sample.html, 'html');
      }
    }
    if (name === 'default') {
      const outputs = await Promise.all(samples.flatMap(sample => [0, 1].map(() => translate(sample, sample.short, 'concurrent'))));
      assert.equal(outputs[0], outputs[1], 'Concurrent English output changed');
      assert.equal(outputs[2], outputs[3], 'Concurrent Chinese output changed');
    }
    assertSettings(JSON.parse((await request('/ui/api/settings')).body), runtime);
    if (name === 'default') {
      const accepted = new Set();
      let settled = 0, stopping = false;
      const pending = [];
      const observed = new Promise((resolve, reject) => {
        child.on('message', message => {
          if (message.accepted) accepted.add(message.accepted);
          if (message.stopping) {
            stopping = true;
            result.stop ??= { accepted: accepted.size, pending_at_stop: 4 - settled };
            result.stop.active_in_server = message.active;
            if (!message.active.length) reject(new Error('No in-flight request when the product handled stop'));
            else resolve();
          }
          if (accepted.size === 4 && !result.stop) {
            result.stop = { accepted: accepted.size, pending_at_stop: 4 - settled };
            if (settled === 4) { reject(new Error('Drain test had no outstanding HTTP request')); return; }
            stop().catch(reject);
          }
        });
      });
      for (let i = 0; i < 4; i++) {
        const sample = samples[i % 2];
        pending.push(translate(sample, (sample.paragraph + '\n').repeat(60), 'stop-drain', String(i + 1)).finally(() => { settled++; }));
      }
      await within(Promise.all([observed, ...pending]), context.timeout, 'Timed out observing stop/drain');
      assert(stopping);
      result.stop.completed = settled;
    } else { await stop(); result.stop = { accepted: 0 }; }
    const stopped = await within(closed, 15000, 'Service did not stop cleanly');
    result.stop.exit = stopped;
    assert.equal(stopped.code, 0, `Service stop failed: ${JSON.stringify(stopped)}`);
    assert.match(logs, /Server shutdown complete/);
    const announcement = logs.indexOf('is running!');
    assert(announcement >= 0, 'Missing ready announcement');
    for (const sample of samples) {
      const warmed = logs.indexOf(`Engine created successfully for ${sample.from} -> ${sample.to}`);
      assert(warmed >= 0 && warmed < announcement, 'Ready announcement preceded warmup');
    }
    assert.doesNotMatch(logs, /Forced shutdown|warmup failed|Uncaught Exception|Unhandled Rejection|Downloading latest records/i);
    result.status = 'passed';
  } finally {
    result.exit_before_cleanup = exit ?? null;
    if (!exit) {
      result.cleanup_requested_stop = child.connected;
      if (child.connected) await stop().catch(() => {});
      await within(closed, 2000, 'Cleanup grace period expired').catch(() => {});
      if (!exit) { child.kill('SIGKILL'); await closed; }
    }
    result.exit = exit;
    if (result.status !== 'passed') result.service_log_tail = logs.slice(-16000);
    await writeFile(result.log, logs);
  }
}

export async function verify(config) {
  const report = { schemaVersion: 1, status: 'running', platform: process.platform, arch: process.arch,
    node: process.version, os_release: release(), cpu: cpus()[0]?.model,
    runtime: config.runtime, entry: config.entry, models_source: config.models, started_at: new Date().toISOString(), scenarios: [] };
  try {
    assert(Number(process.versions.node.split('.')[0]) >= 22, 'Node 22 or later is required');
    assert(['linux', 'darwin', 'win32'].includes(process.platform) && ['x64', 'arm64'].includes(process.arch), 'Unsupported platform');
    await mkdir(config.work, { recursive: true });
    const directory = await mkdtemp(path.join(config.work, 'HTTP 验收 '));
    report.directory = directory;
    report.main_sha256 = await hashFile(config.entry);
    const addon = path.join(path.dirname(config.entry), `onnx-async-${process.platform}-${process.arch}.node`);
    report.addon_sha256 = await hashFile(addon);
    const registryPath = new URL('./ssru-platform-records.json', import.meta.url);
    const registry = await json(registryPath);
    report.records_sha256 = await hashFile(registryPath);
    report.records_provenance = registry.provenance;
    const prepared = await prepareModels(config.models, directory, registry, await json(new URL('./ssru-platform-models.json', import.meta.url)));
    report.models = prepared.evidence;
    const context = { ...config, ...prepared, directory, registry };
    for (const [name, runtime] of [['default', 'native-ssru'], ['disabled', 'native-async'], ['missing-addon', undefined]]) {
      let entry = config.entry;
      if (name === 'missing-addon') {
        const isolated = path.join(directory, 'missing-addon', 'app');
        await cp(path.dirname(entry), isolated, { recursive: true, filter: filename => !/^onnx-async-.*\.node$/.test(path.basename(filename)) });
        await writeFile(path.join(isolated, 'package.json'), '{"type":"module"}\n');
        const require = createRequire(pathToFileURL(config.entry));
        for (const dependency of ['onnxruntime-node', '@sctg/sentencepiece-js']) {
          let location = path.dirname(require.resolve(dependency));
          while ((await json(path.join(location, 'package.json')).catch(() => ({}))).name !== dependency) {
            const parent = path.dirname(location);
            assert.notEqual(parent, location, `Cannot locate package root: ${dependency}`);
            location = parent;
          }
          const target = path.join(isolated, 'node_modules', dependency);
          await mkdir(path.dirname(target), { recursive: true });
          if (!await access(target).then(() => true, () => false))
            await symlink(location, target, process.platform === 'win32' ? 'junction' : 'dir');
        }
        entry = path.join(isolated, path.basename(entry));
        assert.equal(await hashFile(entry), report.main_sha256, 'Isolated product entry changed');
        for (const candidate of [path.join(isolated, path.basename(addon)), path.resolve(isolated, '../../dist', path.basename(addon))]) {
          assert.equal(await access(candidate).then(() => true, () => false), false, 'Addon isolation failed');
        }
      }
      const result = { name };
      report.scenarios.push(result);
      await scenario(name, entry, runtime, context, result);
      if (name === 'default') {
        for (const model of report.models) {
          const directory = path.join(prepared.models, model.pair);
          const generated = (await readdir(directory)).filter(name => /^native-ssru-.*\.onnx$/.test(name));
          assert.equal(generated.length, 1, 'Expected one product-generated native SSRU graph');
          assert.equal(await hashFile(path.join(directory, generated[0])), model.ssru_sha256, 'Product SSRU graph differs from platform fixture');
        }
      }
    }
    for (const model of report.models) {
      assert.equal(await hashFile(path.join(prepared.models, model.pair, 'model.onnx')), model.fast_sha256, 'Product rebuilt the fixed Fast baseline');
    }
    assert.equal(await hashFile(config.entry), report.main_sha256, 'Shared build entry changed during verification');
    assert.equal(await hashFile(addon), report.addon_sha256, 'Shared addon changed during verification');
    report.status = 'passed';
  } catch (error) {
    report.status = 'failed';
    report.error = inspect(error, { depth: 4 });
    const active = report.scenarios.find(row => row.status === 'running');
    if (active) { active.status = 'failed'; active.error = report.error; }
  } finally {
    report.finished_at = new Date().toISOString();
    await save(config.report, report);
  }
  return report;
}

if (process.argv[1] && pathToFileURL(path.resolve(process.argv[1])).href === import.meta.url) {
  try {
    const config = options();
    if (config.help) console.log('Usage: node scripts/verify-fast-service.mjs [path/to/dist/main.js] [--dist DIR] [--runtime PATH] [--models DIR] [--work-dir DIR] [--report FILE] [--timeout-ms 180000]\nInputs: fixed-hash source files + fast.onnx + ssru.onnx from verify-ssru-platform; no downloads. Electron hosts automatically use ELECTRON_RUN_AS_NODE=1 (CPU HTTP only).');
    else {
      const report = await verify(config);
      console.log(`${report.status === 'passed' ? 'PASS' : 'FAIL'} ${process.platform}/${process.arch}: ${config.report}`);
      if (report.error) { console.error(report.error); process.exitCode = 1; }
    }
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
