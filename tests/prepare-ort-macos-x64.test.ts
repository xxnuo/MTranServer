import { afterEach, expect, test } from 'bun:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { files, installBundle, smokeScript, source, verifyBundle, version } from '../scripts/prepare-ort-macos-x64';

const root = path.resolve(import.meta.dir, '..');
const directories: string[] = [];
const hash = (bytes: Uint8Array) => createHash('sha256').update(bytes).digest('hex');

function fixture() {
  mkdirSync(path.join(root, '.tmp'), { recursive: true });
  const directory = mkdtempSync(path.join(root, '.tmp/prepare-ort-macos-x64-test-中文 路径-'));
  directories.push(directory);
  const bundle = path.join(directory, 'dist/ort/darwin/x64');
  mkdirSync(bundle, { recursive: true });
  // Synthetic Mach-O headers are only for integrity/installation tests; the smoke test below uses real ORT.
  const library = Buffer.alloc(32);
  library.writeUInt32LE(0xfeedfacf, 0);
  library.writeUInt32LE(0x01000007, 4);
  const binding = Buffer.from(library);
  binding[16] = 1;
  for (const name of files) writeFileSync(path.join(bundle, name), name.endsWith('.node') ? binding : library);
  for (const name of ['LICENSE', 'ThirdPartyNotices.txt']) writeFileSync(path.join(bundle, name), 'test notice');
  const manifest = { version, runtimeVersion: version, platform: 'darwin', arch: 'x64', source: { ...source },
    builtAt: new Date().toISOString(), node: process.versions.node, deploymentTarget: '13.0',
    dependencyLockSha256: '1'.repeat(64), cmakeDependenciesSha256: '2'.repeat(64),
    files: Object.fromEntries([...files, 'LICENSE', 'ThirdPartyNotices.txt'].map(name => [name, hash(readFileSync(path.join(bundle, name)))])) };
  const save = () => writeFileSync(path.join(bundle, 'provenance.json'), JSON.stringify(manifest));
  save();
  return { directory, bundle, manifest, save };
}

afterEach(() => { for (const directory of directories.splice(0)) rmSync(directory, { recursive: true, force: true }); });

test('accepts a complete, hashed 1.30.0 pair at a path with Chinese characters and spaces', () => {
  const { bundle, manifest } = fixture();
  expect(verifyBundle(bundle)).toEqual(manifest);
});

test.each(['version', 'runtimeVersion', 'arch', 'source'] as const)('rejects a bundle with the wrong %s', field => {
  const { bundle, manifest, save } = fixture();
  if (field === 'source') manifest.source.commit = '0'.repeat(40);
  else manifest[field] = field === 'arch' ? 'arm64' : '1.31.1';
  save();
  expect(() => verifyBundle(bundle)).toThrow();
});

test('rejects missing, corrupted, wrong-architecture and mixed-version library files', () => {
  const { bundle, manifest, save } = fixture();
  const filename = path.join(bundle, files[1]);
  const bytes = readFileSync(filename);
  rmSync(filename);
  expect(() => verifyBundle(bundle)).toThrow();
  writeFileSync(filename, Buffer.concat([bytes, Buffer.of(1)]));
  expect(() => verifyBundle(bundle)).toThrow('checksum mismatch');
  bytes.writeUInt32LE(0x0100000c, 4);
  writeFileSync(filename, bytes);
  manifest.files[files[1]] = hash(bytes);
  save();
  expect(() => verifyBundle(bundle)).toThrow('x86_64 Mach-O');
  bytes.writeUInt32LE(0x01000007, 4);
  bytes[20] = 1;
  writeFileSync(filename, bytes);
  manifest.files[files[1]] = hash(bytes);
  save();
  expect(() => verifyBundle(bundle)).toThrow('library alias must match');
});

test('restores the dependency directory and rolls back when Node import or inference fails', () => {
  const { directory, bundle } = fixture();
  const destination = path.join(directory, 'node_modules/onnxruntime-node/bin/napi-v6/darwin/x64');
  mkdirSync(destination, { recursive: true });
  writeFileSync(path.join(destination, 'previous'), 'keep');
  expect(() => installBundle(bundle, destination, () => {
    verifyBundle(destination);
    throw new Error('Node inference failed');
  })).toThrow('Node inference failed');
  expect(readFileSync(path.join(destination, 'previous'), 'utf8')).toBe('keep');
  expect(readdirSync(path.dirname(destination))).toEqual(['x64']);
  let verified = false;
  installBundle(bundle, destination, () => {
    verifyBundle(destination);
    verified = true;
  });
  expect(verified).toBe(true);
  expect(existsSync(path.join(destination, 'previous'))).toBe(false);
  expect(readdirSync(path.dirname(destination))).toEqual(['x64']);
  expect(verifyBundle(destination)).toEqual(verifyBundle(bundle));
});

test('does not alter installed files when the packaged bundle is incomplete', () => {
  const { directory, bundle } = fixture();
  const destination = path.join(directory, 'installed');
  mkdirSync(destination);
  writeFileSync(path.join(destination, 'previous'), 'keep');
  rmSync(path.join(bundle, files[0]));
  expect(() => installBundle(bundle, destination, () => assert.fail('Must not run'))).toThrow();
  expect(readdirSync(destination)).toEqual(['previous']);
});

test('the real Node smoke check imports the installed package and runs CPU inference', () => {
  const require = createRequire(import.meta.url);
  const packageJson = require.resolve('onnxruntime-node/package.json');
  const output = execFileSync('node', ['--input-type=module', '-e', smokeScript, packageJson], {
    cwd: root, encoding: 'utf8', timeout: 30_000,
  });
  expect(JSON.parse(output)).toMatchObject({ platform: process.platform, arch: process.arch, ort: version, inference: 'passed' });
}, 35_000);

test('can be bundled as a standalone Node ESM postinstall entry point', async () => {
  const { directory } = fixture();
  const output = path.join(directory, 'dist/prepare-ort-macos-x64.js');
  writeFileSync(path.join(directory, 'package.json'), '{"type":"module"}');
  const build = await Bun.build({ entrypoints: [path.join(root, 'scripts/prepare-ort-macos-x64.ts')],
    outdir: path.dirname(output), target: 'node', format: 'esm' });
  expect(build.success).toBe(true);
  const help = execFileSync('node', [output, '--help'], { encoding: 'utf8' });
  expect(help).toContain('--restore-only');
  expect(help).toContain('provenance.json');
  if (process.platform !== 'darwin' || process.arch !== 'x64') {
    const result = execFileSync('node', [output, '--restore-only'], { encoding: 'utf8' });
    expect(result).toContain('No preparation needed');
    expect(existsSync(path.join(directory, 'node_modules'))).toBe(false);
  }
  const invalid = Bun.spawnSync(['node', output, '--unexpected']);
  expect(invalid.exitCode).toBe(1);
  expect(invalid.stderr.toString()).toContain('Unknown arguments');
});
