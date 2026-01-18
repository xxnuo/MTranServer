import { existsSync, mkdirSync, mkdtempSync, renameSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));

/** Optional native acceleration; standard ORT remains usable when compilation is unavailable. */
export function buildNative(): string | null {
  const output = path.join(root, `dist/onnx-async-${process.platform}-${process.arch}.node`);
  const temporary = `${output}.build-${process.pid}`;
  const fallback = (reason: string) => {
    rmSync(temporary, { force: true });
    rmSync(output, { force: true });
    console.warn(`[native] ${reason}; using standard ORT fallback`);
    return null;
  };
  if (!['linux', 'darwin'].includes(process.platform) || !['x64', 'arm64'].includes(process.arch)) {
    return fallback(`Unsupported platform ${process.platform}-${process.arch}`);
  }
  const candidates = process.env.CXX ? [process.env.CXX] : ['c++', 'g++', 'clang++'];
  const compiler = candidates.find(command => spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0);
  if (!compiler) return fallback('No C++ compiler found');
  const includeCandidates = [
    process.env.NODE_INCLUDE_DIR,
    '/usr/include/node', '/usr/local/include/node',
    '/opt/homebrew/include/node',
    path.resolve(path.dirname(process.execPath), '../include/node'),
  ].filter((directory): directory is string => !!directory);
  const include = includeCandidates.find(directory => existsSync(path.join(directory, 'node_api.h')));
  if (!include) return fallback('Node headers not found (set NODE_INCLUDE_DIR)');
  mkdirSync(path.dirname(output), { recursive: true });
  const native = path.join(root, 'native/onnx-async');
  mkdirSync(path.join(root, '.tmp'), { recursive: true });
  const buildDirectory = mkdtempSync(path.join(root, '.tmp/native-build-'));
  try {
    const packed: string[] = [];
    if (process.arch === 'x64') {
      const object = path.join(buildDirectory, 'packed.o');
      const result = spawnSync(compiler, [
        '-std=c++20', '-O3', '-Wall', '-Wextra', '-fPIC', '-mavx2', '-mavxvnni', '-ffp-contract=off',
        '-I', path.join(root, 'native/ssru/third_party/xsimd/include'),
        '-c', path.join(root, 'native/ssru/packed.cc'), '-o', object,
      ], { encoding: 'utf8', env: { ...process.env, TMPDIR: buildDirectory } });
      if (result.status === 0) packed.push('-DMTRAN_SSRU_AVXVNNI', object);
      else console.warn(`[native] AVXVNNI compilation unavailable; retaining InvokeOp: ${result.error?.message || result.stderr.trim()}`);
    }
    const result = spawnSync(compiler, [
      '-std=c++20', '-O3', '-Wall', '-Wextra', '-shared', '-fPIC', '-pthread',
      '-I', include, '-I', native, path.join(native, 'addon.cc'),
      '-DMTRAN_NATIVE_SSRU', path.join(native, 'ssru_op.cc'), path.join(root, 'native/ssru/ssru.cc'),
      ...packed,
      ...(process.platform === 'darwin' ? ['-undefined', 'dynamic_lookup'] : ['-ldl']),
      '-o', temporary,
    ], { encoding: 'utf8', env: { ...process.env, TMPDIR: buildDirectory } });
    if (result.status !== 0) {
      return fallback(`Compilation failed: ${result.error?.message || result.stderr.trim() || `exit ${result.status}`}`);
    }
    renameSync(temporary, output);
    console.log(`[native] Built ${path.relative(root, output)}`);
    return output;
  } finally {
    rmSync(buildDirectory, { recursive: true, force: true });
  }
}

if (import.meta.main) buildNative();
