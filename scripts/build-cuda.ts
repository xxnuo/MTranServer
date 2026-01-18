import { existsSync, mkdirSync, cpSync, rmSync, renameSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));

export function buildCuda(required = false): string | null {
  const sdk = process.env.MLX_CUDA_SDK;
  if (!sdk && !required) return null;
  const output = path.join(root, 'dist/cuda');
  const staging = `${output}.build-${process.pid}`;
  try {
    if (process.platform !== 'linux' || !['arm64', 'x64'].includes(process.arch)) throw new Error('CUDA runtime requires Linux arm64 or x64');
    if (!sdk || !['include/mlx/mlx.h', 'lib/libmlx.so', 'include/cccl/cuda/std/tuple', 'include/cute/numeric/numeric_types.hpp'].every(file => existsSync(path.join(sdk, file)))) {
      throw new Error('Set MLX_CUDA_SDK to an MLX CUDA SDK built for this GPU; system CUDA and NCCL libraries must be available');
    }
    mkdirSync(path.join(staging, 'lib'), { recursive: true });
    const result = spawnSync(process.env.CXX || 'c++', [
      '-std=c++20', '-O3', '-Wall', '-Wextra', '-DMTRAN_CUDA', '-isystem', path.join(sdk, 'include'),
      path.join(root, 'native/metal/worker.cc'), '-L', path.join(sdk, 'lib'), '-lmlx',
      '-Wl,-rpath,$ORIGIN/lib', '-o', path.join(staging, 'metal-worker'),
    ], { encoding: 'utf8', timeout: 120_000 });
    if (result.status !== 0) throw new Error(result.error?.message || result.stderr.trim() || 'CUDA compilation failed');
    cpSync(path.join(sdk, 'lib/libmlx.so'), path.join(staging, 'lib/libmlx.so'));
    cpSync(path.join(sdk, 'include'), path.join(staging, 'include'), { recursive: true });
    cpSync(path.join(root, 'native/metal/LICENSE.mlx'), path.join(staging, 'LICENSE.mlx'));
    for (const name of ['LICENSE.cccl', 'LICENSE.cutlass']) cpSync(path.join(root, 'native/metal', name), path.join(staging, name));
    const probe = spawnSync(path.join(staging, 'metal-worker'), ['--check'], { encoding: 'utf8', timeout: 120_000,
      env: { ...process.env, MLX_PTX_CACHE_DIR: path.join(staging, '.check-ptx') },
    });
    if (probe.status !== 0 || probe.stdout.trim() !== 'CUDA_RUNTIME_OK') throw new Error(probe.error?.message || probe.stderr.trim() || 'CUDA runtime self-check failed; check GPU architecture and system CUDA/NCCL libraries');
    rmSync(path.join(staging, '.check-ptx'), { recursive: true, force: true });
    rmSync(output, { recursive: true, force: true });
    renameSync(staging, output);
    console.log('[cuda] Built and validated GPU runtime (uses system CUDA/NCCL libraries)');
    return output;
  } catch (error) {
    rmSync(staging, { recursive: true, force: true });
    throw error;
  }
}

if (import.meta.main) buildCuda(true);
