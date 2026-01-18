import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, renameSync, rmSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import * as os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// v1.30.0's install-metadata.js has no darwin/x64 downloads; its release has no Intel Mac asset.
// Official npm tar SHA-256: 6e3390d6b783e7be946fad629292799da28d0b42f84856e50d2c1b0383291e75.
// https://github.com/microsoft/onnxruntime/blob/v1.30.0/js/node/script/install-metadata.js
export const source = {
  repository: 'https://github.com/microsoft/onnxruntime.git',
  tag: 'v1.30.0',
  commit: 'f2c39fe2f838cf35ce7da92824f5a5e3ee6e88a7',
};
export const version = '1.30.0';
export const files = ['onnxruntime_binding.node', `libonnxruntime.${version}.dylib`, 'libonnxruntime.1.dylib'] as const;
const notices = ['LICENSE', 'ThirdPartyNotices.txt'];
const manifestName = 'provenance.json';
const rootDirectory = fileURLToPath(new URL('..', import.meta.url));
const sha256 = (file: string) => createHash('sha256').update(readFileSync(file)).digest('hex');

interface Provenance {
  version: string;
  platform: string;
  arch: string;
  source: typeof source;
  runtimeVersion: string;
  files: Record<string, string>;
  builtAt: string;
  node: string;
  deploymentTarget: string;
  dependencyLockSha256: string;
  cmakeDependenciesSha256: string;
}

export function verifyBundle(directory: string): Provenance {
  const manifest: Provenance = JSON.parse(readFileSync(path.join(directory, manifestName), 'utf8'));
  assert.equal(manifest.version, version, 'ORT bundle version mismatch');
  assert.equal(manifest.runtimeVersion, version, 'ORT shared library version mismatch');
  assert.equal(manifest.platform, 'darwin');
  assert.equal(manifest.arch, 'x64');
  assert.deepEqual(manifest.source, source, 'ORT bundle must come from the pinned official source');
  for (const name of [...files, ...notices]) {
    const file = path.join(directory, name);
    assert.match(manifest.files[name] ?? '', /^[a-f0-9]{64}$/, `Missing SHA-256: ${name}`);
    assert.equal(sha256(file), manifest.files[name], `ORT checksum mismatch: ${name}`);
    if (notices.includes(name)) continue;
    const bytes = readFileSync(file);
    assert(bytes.length >= 8 && bytes.readUInt32LE(0) === 0xfeedfacf && bytes.readUInt32LE(4) === 0x01000007,
      `Expected a native x86_64 Mach-O binary: ${name}`);
  }
  assert.equal(manifest.files[files[1]], manifest.files[files[2]], 'ORT library alias must match the versioned library');
  return manifest;
}

// Uses a real Node process: Bun loading a binding does not verify the published Node entry point.
export const smokeScript = `
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
const require = createRequire(process.argv[1]);
assert.equal(require('onnxruntime-node/package.json').version, '${version}');
const ort = (await import(pathToFileURL(require.resolve('onnxruntime-node')).href)).default;
assert.equal(ort.env.versions.node, '${version}');
assert.equal(ort.env.versions.common, '${version}');
// ONNX IR 8, opset 13: Identity(float32[2] X) -> Y.
const model = Buffer.from('CAg6QQoQCgFYEgFZIghJZGVudGl0eRILb3J0X3ByZXBhcmVaDwoBWBIKCggIARIECgIIAmIPCgFZEgoKCAgBEgQKAggCQgQKABAN', 'base64');
const session = await ort.InferenceSession.create(model, {executionProviders: ['cpu'], intraOpNumThreads: 1});
try {
  const result = await session.run({X: new ort.Tensor('float32', Float32Array.of(3, -7), [2])});
  assert.deepEqual(result.Y.dims, [2]);
  assert.deepEqual(Array.from(result.Y.data), [3, -7]);
  result.Y.dispose();
} finally { await session.release(); }
console.log(JSON.stringify({node: process.version, platform: process.platform, arch: process.arch, ort: ort.env.versions.node, inference: 'passed'}));
`;

export function installBundle(bundle: string, destination: string, verifyInstalled: () => void): void {
  verifyBundle(bundle);
  mkdirSync(path.dirname(destination), { recursive: true });
  const stage = mkdtempSync(`${destination}.prepare-`);
  const backup = `${stage}.previous`;
  let installed = false;
  try {
    for (const name of [...files, ...notices, manifestName]) copyFileSync(path.join(bundle, name), path.join(stage, name));
    verifyBundle(stage);
    if (existsSync(destination)) renameSync(destination, backup);
    renameSync(stage, destination);
    installed = true;
    verifyInstalled();
  } catch (error) {
    if (installed) rmSync(destination, { recursive: true, force: true });
    if (existsSync(backup)) renameSync(backup, destination);
    throw error;
  } finally {
    rmSync(stage, { recursive: true, force: true });
  }
  rmSync(backup, { recursive: true, force: true });
}

function buildBundle(root: string, cache: string): string {
  const temporary = path.join(cache, 'tmp');
  mkdirSync(temporary, { recursive: true });
  const env = { ...process.env, TMPDIR: temporary, npm_config_cache: path.join(cache, 'npm-cache'),
    npm_config_strict_ssl: 'true', GIT_SSL_NO_VERIFY: 'false', NODE_TLS_REJECT_UNAUTHORIZED: '1', CMAKE_TLS_VERIFY: '1' };
  const capture = (command: string, args: string[], cwd = root) =>
    execFileSync(command, args, { cwd, env, encoding: 'utf8' }).trim();
  const run = (command: string, args: string[], cwd = root) => {
    const result = spawnSync(command, args, { cwd, env, stdio: 'inherit' });
    assert.equal(result.status, 0, `${command} failed: ${result.error?.message ?? `exit ${result.status}`}`);
  };
  for (const command of ['git', 'uv', 'cmake', 'ninja', 'npm']) capture(command, ['--version']);
  capture('xcrun', ['--find', 'clang++']);
  const node = JSON.parse(capture('node', ['-p', 'JSON.stringify({platform:process.platform,arch:process.arch,version:process.versions.node})']));
  assert.equal(node.platform, 'darwin');
  assert.equal(node.arch, 'x64', 'Build with native Intel Node.js');
  const [major, minor] = node.version.split('.').map(Number);
  assert(major > 22 || (major === 22 && minor >= 9), 'Source build requires Node.js >=22.9 (upstream cmake-js requirement)');
  const checkout = path.join(cache, `source-${source.commit}`);
  if (!existsSync(checkout)) run('git', ['-c', 'http.sslVerify=true', 'clone', '--depth', '1', '--branch', source.tag,
    '--single-branch', source.repository, checkout]);
  assert.equal(capture('git', ['rev-parse', 'HEAD'], checkout), source.commit, 'Unexpected official source revision');
  assert.equal(capture('git', ['status', '--porcelain', '--untracked-files=no'], checkout), '', 'Cached ORT source has tracked changes');
  assert.equal(readFileSync(path.join(checkout, 'VERSION_NUMBER'), 'utf8').trim(), version);
  const binding = path.join(checkout, 'js/node');
  assert.equal(JSON.parse(readFileSync(path.join(binding, 'package.json'), 'utf8')).version, version);
  const build = path.join(cache, 'build');
  const jobs = String(Math.min(os.availableParallelism?.() ?? os.cpus().length, 4));
  const deploymentTarget = '13.0';
  // All CMake dependencies have pinned hashes in cmake/deps.txt; unused git submodules are unnecessary.
  run('uv', ['run', '--no-project', '--python', '3.12', 'python', 'tools/ci_build/build.py', '--update', '--build',
    '--build_dir', build, '--config', 'Release', '--build_shared_lib', '--target', 'onnxruntime', '--skip_tests', '--skip_submodule_sync',
    '--parallel', jobs, '--cmake_generator', 'Ninja', '--osx_arch', 'x86_64', '--apple_deploy_target', deploymentTarget,
    '--compile_no_warning_as_error', '--cmake_extra_defines', 'onnxruntime_BUILD_UNIT_TESTS=OFF', 'CMAKE_TLS_VERIFY=ON'], checkout);
  // Upstream uses npm with its own package-lock.json; ignore lifecycle scripts and use its CMakeLists directly.
  // Its script/build.ts uses shell:true, which breaks paths containing spaces.
  run('npm', ['ci', '--ignore-scripts', '--no-audit', '--no-fund', '--strict-ssl=true'], binding);
  run('node', ['node_modules/cmake-js/bin/cmake-js', 'configure', '--arch=x64', '--CDnapi_build_version=6',
    '--CDCMAKE_BUILD_TYPE=Release', `--CDONNXRUNTIME_BUILD_DIR=${path.join(build, 'Release')}`,
    '--CDONNXRUNTIME_GENERATOR=Ninja', '--CDCMAKE_OSX_ARCHITECTURES=x86_64',
    `--CDCMAKE_OSX_DEPLOYMENT_TARGET=${deploymentTarget}`, '--CDCMAKE_TLS_VERIFY=ON'], binding);
  run('cmake', ['--build', path.join(binding, 'build'), '--config', 'Release', '--parallel', jobs]);
  const output = path.join(binding, 'bin/napi-v6/darwin/x64');
  for (const name of notices) copyFileSync(path.join(checkout, name), path.join(output, name));
  const runtimeVersion = capture('uv', ['run', '--no-project', '--python', '3.12', 'python', '-c', `
import ctypes, sys
class ApiBase(ctypes.Structure):
    _fields_ = [('get_api', ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_uint32)),
                ('get_version', ctypes.CFUNCTYPE(ctypes.c_char_p))]
lib = ctypes.CDLL(sys.argv[1])
lib.OrtGetApiBase.restype = ctypes.POINTER(ApiBase)
base = lib.OrtGetApiBase().contents
assert base.get_api(30), 'Missing ORT API 30'
print(base.get_version().decode())
`, path.join(output, files[1])]);
  assert.equal(runtimeVersion, version, 'Built ORT runtime version mismatch');
  const provenance: Provenance = { version, platform: 'darwin', arch: 'x64', source, runtimeVersion,
    files: Object.fromEntries([...files, ...notices].map(name => [name, sha256(path.join(output, name))])),
    builtAt: new Date().toISOString(), node: node.version, deploymentTarget,
    dependencyLockSha256: sha256(path.join(binding, 'package-lock.json')),
    cmakeDependenciesSha256: sha256(path.join(checkout, 'cmake/deps.txt')) };
  writeFileSync(path.join(output, manifestName), JSON.stringify(provenance, null, 2) + '\n');
  verifyBundle(output);
  return output;
}

export function prepareOrtMacosX64(root = rootDirectory, restoreOnly = false): void {
  if (process.platform !== 'darwin' || process.arch !== 'x64') {
    console.log(`[ort] No preparation needed for ${process.platform}/${process.arch}`);
    return;
  }
  const require = createRequire(path.join(root, 'package.json'));
  const packageJson = require.resolve('onnxruntime-node/package.json');
  assert.equal(JSON.parse(readFileSync(packageJson, 'utf8')).version, version, 'Installed onnxruntime-node must remain 1.30.0');
  const portable = path.join(root, 'dist/ort/darwin/x64');
  let bundle = portable;
  if (!existsSync(bundle)) {
    assert(!restoreOnly, `Missing packaged ORT artifacts: ${portable}; build them on an Intel Mac before publishing`);
    const translated = spawnSync('sysctl', ['-in', 'sysctl.proc_translated'], { encoding: 'utf8' });
    assert.notEqual(translated.stdout?.trim(), '1', 'Source builds require a native Intel Mac runner');
    bundle = buildBundle(root, path.join(root, '.tmp/prepare-ort-macos-x64'));
  }
  const destination = path.join(path.dirname(packageJson), 'bin/napi-v6/darwin/x64');
  installBundle(bundle, destination, () => {
    execFileSync(process.versions.bun ? 'node' : process.execPath, ['--input-type=module', '-e', smokeScript, packageJson], {
      cwd: root, env: { ...process.env, NODE_TLS_REJECT_UNAUTHORIZED: '1',
        ...(process.versions.electron ? { ELECTRON_RUN_AS_NODE: '1' } : {}) }, stdio: restoreOnly ? 'pipe' : 'inherit', timeout: 120_000,
    });
  });
  if (bundle !== portable) {
    mkdirSync(portable, { recursive: true });
    for (const name of [...files, ...notices, manifestName]) copyFileSync(path.join(bundle, name), path.join(portable, name));
    verifyBundle(portable);
  }
  if (!restoreOnly) console.log(`[ort] Prepared ${version}: ${destination}\n[ort] Publish artifacts: ${portable}`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  if (args.length === 1 && args[0] === '--help') {
    console.log('Prepare ORT 1.30.0 on native macOS x64 (other platforms: no-op).\n' +
      'Default: restore dist/ort/darwin/x64 or build the pinned official source, then verify Node import + inference.\n' +
      '--restore-only: require packaged artifacts; no downloads or compiler needed.\n' +
      'Source build prerequisites: Xcode CLI tools, Node.js >=22.9 + npm, uv, git, CMake >=3.28, Ninja.\n' +
      'CI: upload node_modules/onnxruntime-node/bin/napi-v6/darwin/x64 including provenance.json.\n' +
      'Publish: include dist/ort/darwin/x64 and bundle this script as dist/prepare-ort-macos-x64.js (Node ESM);\n' +
      'invoke it with --restore-only in postinstall, before any application import. Minimum macOS: 13.0.');
  } else {
    try {
      assert(args.length === 0 || (args.length === 1 && args[0] === '--restore-only'), 'Unknown arguments (see --help)');
      prepareOrtMacosX64(rootDirectory, args[0] === '--restore-only');
    } catch (error) {
      console.error(`[ort] ${error instanceof Error ? error.message : error}`);
      process.exitCode = 1;
    }
  }
}
