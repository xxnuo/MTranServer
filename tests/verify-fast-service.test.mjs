import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdir, mkdtemp, readFile, rm } from 'node:fs/promises';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { assertSettings, assertTranslation, options, servicePreload, verify } from '../scripts/verify-fast-service.mjs';

test('CLI accepts Unicode/space paths and rejects ambiguous or invalid options', () => {
  const result = options(['发行 包/main.js', '--runtime', '宿主 路径/electron', '--models', '模型 文件', '--work-dir', '.tmp/验收 路径', '--report', '.tmp/报告 文件.json']);
  assert.equal(result.entry, path.resolve('发行 包/main.js'));
  assert.equal(result.models, path.resolve('模型 文件'));
  assert.equal(result.runtime, path.resolve('宿主 路径/electron'));
  assert.equal(result.report, path.resolve('.tmp/报告 文件.json'));
  assert.equal(options(['--dist', '发行 包']).entry, path.resolve('发行 包/main.js'));
  assert.throws(() => options(['main.js', '--dist', 'dist']));
  for (const value of ['0', '-1', 'NaN', '1.5', '2147483648']) assert.throws(() => options([`--timeout-ms=${value}`]));
});

test('first settings response requires both warm engines and the exact runtime', () => {
  const settings = { status: 'running', config: { server: { fast: true, cacheSize: 0, enableOfflineMode: true, checkUpdate: false } },
    engines: [{ from: 'en', to: 'zh-Hans', provider: 'cpu', runtime: 'native-ssru' },
      { from: 'zh-Hans', to: 'en', provider: 'cpu', runtime: 'native-ssru' }] };
  assertSettings(settings, 'native-ssru');
  assert.throws(() => assertSettings({ ...settings, engines: [] }, 'native-ssru'));
  assert.throws(() => assertSettings(settings, 'native-async'));
  settings.config.server.cacheSize = 1;
  assert.throws(() => assertSettings(settings, 'native-ssru'));
  settings.config.server.cacheSize = 0;
  for (const engine of settings.engines) { delete engine.runtime; engine.fallbackReason = 'Native SSRU addon unavailable'; }
  assertSettings(settings, undefined);
  delete settings.engines[0].fallbackReason;
  assert.throws(() => assertSettings(settings, undefined));
});

test('translation checks reject empty, unchanged, wrong-language and broken HTML outputs', () => {
  const sample = { from: 'en', to: 'zh-Hans' };
  assertTranslation('图书馆开门。', sample, 'The library is open.');
  for (const result of ['', 'The library is open.', 'Still English.'])
    assert.throws(() => assertTranslation(result, sample, 'The library is open.'));
  const html = '<p data-test="keep">Hello <strong>world.</strong></p>';
  assertTranslation('<p data-test="keep">你好<strong>世界。</strong></p>', sample, html, true);
  assert.throws(() => assertTranslation('<p>你好<strong>世界。</strong></p>', sample, html, true));
  assert.throws(() => assertTranslation('Hello 世界', { to: 'en' }, '你好世界'));
});

test('pinned records and baseline manifests exactly match the source fixture hashes', async () => {
  const registry = JSON.parse(await readFile(new URL('../scripts/ssru-platform-records.json', import.meta.url)));
  const fixtures = JSON.parse(await readFile(new URL('../scripts/ssru-platform-models.json', import.meta.url)));
  assert.equal(registry.provenance.sources.length, 2);
  assert.equal(registry.data.length, 7);
  for (const fixture of fixtures) {
    const baseline = registry.manifests.find(row => row.pair === fixture.pair);
    assert.match(baseline.identity, /^[a-f0-9]{64}$/);
    assert.match(baseline.sourceRevision, /^[a-f0-9]{64}$/);
    for (const file of fixture.files) {
      const record = registry.data.find(row => `${row.sourceLanguage}_${row.targetLanguage}` === fixture.pair
        && row.attachment.filename === file.path + '.zst');
      assert.equal(record.decompressedHash, file.sha256);
      assert.equal(record.attachment.hash, file.downloadSha256);
      assert.equal(baseline.files.find(row => row.path === file.path).sha256, file.sha256);
    }
  }
});

test('verification failures still produce a machine-readable report', async () => {
  const temporary = fileURLToPath(new URL('../.tmp', import.meta.url));
  await mkdir(temporary, { recursive: true });
  const directory = await mkdtemp(path.join(temporary, 'verify-fast-service-'));
  try {
    const config = options([path.join(directory, 'missing/main.js'), '--work-dir', directory, '--report', path.join(directory, 'report.json')]);
    const result = await verify(config);
    assert.equal(result.status, 'failed');
    assert.match(result.error, /ENOENT/);
    assert.deepEqual(JSON.parse(await readFile(config.report, 'utf8')), result);
  } finally { await rm(directory, { recursive: true, force: true }); }
});

test('portable IPC stop calls the real signal handler with an accepted HTTP request still active', { timeout: 10000 }, async () => {
  const program = `
    import { createServer } from 'node:http';
    const server = createServer((req, res) => setTimeout(() => res.end('drained'), 100));
    process.on('SIGTERM', () => server.close(() => process.exit(0)));
    server.listen(0, '127.0.0.1', () => process.send({ port: server.address().port }));
  `;
  const child = spawn(process.versions.bun ? 'node' : process.execPath, ['--import', 'data:text/javascript,' + encodeURIComponent(servicePreload),
    '--input-type=module', '-e', program], { stdio: ['ignore', 'ignore', 'pipe', 'ipc'] });
  const exited = once(child, 'exit');
  try {
    let host;
    const port = await new Promise(resolve => child.on('message', message => {
      if (message.host) host = message.host;
      if (message.port) resolve(message.port);
    }));
    assert.equal(host.electron, null);
    assert.equal(host.platform, process.platform);
    const accepted = once(child, 'message');
    const response = fetch(`http://127.0.0.1:${port}`, { headers: { 'x-verify-drain': '1', connection: 'close' }, signal: AbortSignal.timeout(5000) });
    response.catch(() => {});
    assert.deepEqual((await accepted)[0], { accepted: '1' });
    const stopping = once(child, 'message');
    child.send('verify-fast-service:stop');
    assert.deepEqual((await stopping)[0], { stopping: true, active: ['1'] });
    assert.equal(await (await response).text(), 'drained');
    assert.deepEqual(await exited, [0, null]);
  } finally { if (child.exitCode === null) child.kill('SIGKILL'); }
});
