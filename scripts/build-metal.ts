import { existsSync, mkdirSync, cpSync, rmSync, renameSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));
const version = '0.32.4';

export function buildMetal(download = false): string | null {
  if (process.platform !== 'darwin' || process.arch !== 'arm64') {
    if (download) throw new Error('Metal runtime requires macOS arm64');
    return null;
  }
  const output = path.join(root, 'dist/metal');
  const staging = `${output}.build-${process.pid}`;
  try {
    let sdk = process.env.MLX_SDK;
    if (!sdk) {
      const probe = spawnSync('uv', ['run', ...(!download ? ['--offline'] : []), '--no-project', '--python', '3.13', '--with', `mlx==${version}`, 'python', '-c', 'import pathlib, mlx.core; print(pathlib.Path(mlx.core.__file__).parent)'], { encoding: 'utf8', timeout: download ? 180_000 : 10_000 });
      if (probe.status !== 0) throw new Error(probe.error?.message || probe.stderr.trim() || 'MLX SDK unavailable');
      sdk = probe.stdout.trim();
    }
    if (!existsSync(path.join(sdk, 'include/mlx/mlx.h'))) throw new Error('MLX headers unavailable');
    mkdirSync(staging, { recursive: true });
    const result = spawnSync(process.env.CXX || 'c++', [
      '-std=c++20', '-O3', '-Wall', '-Wextra', '-isystem', path.join(sdk, 'include'),
      path.join(root, 'native/metal/worker.cc'), '-L', path.join(sdk, 'lib'), '-lmlx',
      '-Wl,-rpath,@executable_path', '-o', path.join(staging, 'metal-worker'),
    ], { encoding: 'utf8', timeout: 120_000 });
    if (result.status !== 0) throw new Error(result.error?.message || result.stderr.trim() || 'Metal compilation failed');
    for (const name of ['libmlx.dylib', 'libjaccl.dylib', 'mlx.metallib']) cpSync(path.join(sdk, 'lib', name), path.join(staging, name));
    cpSync(path.join(root, 'native/metal/LICENSE.mlx'), path.join(staging, 'LICENSE.mlx'));
    const signing = spawnSync('codesign', ['--force', '--sign', '-', path.join(staging, 'metal-worker')], { encoding: 'utf8' });
    if (signing.status !== 0) throw new Error(signing.error?.message || signing.stderr.trim() || 'Metal signing failed');
    rmSync(output, { recursive: true, force: true });
    renameSync(staging, output);
    console.log('[metal] Built standalone Apple GPU runtime');
    return output;
  } catch (error) {
    rmSync(staging, { recursive: true, force: true });
    if (download) throw error;
    console.warn(`[metal] ${error}; CPU inference remains available (build:metal installs the SDK)`);
    return null;
  }
}

if (import.meta.main) buildMetal(true);
