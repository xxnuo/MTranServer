import { existsSync, mkdirSync, mkdtempSync, renameSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { homedir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));

/** Optional native acceleration; standard ORT remains usable when compilation is unavailable. */
export function buildNative(): string | null {
  // Bun may run under x64 emulation while the installed Node is native ARM64.
  const node = spawnSync('node', ['-p', 'process.versions.node + " " + process.arch'], { encoding: 'utf8' });
  const [version, arch] = node.status === 0 ? node.stdout.trim().split(' ') : [process.versions.node, process.arch];
  const output = path.join(root, `dist/onnx-async-${process.platform}-${arch}.node`);
  const temporary = `${output}.build-${process.pid}`;
  const fallback = (reason: string) => {
    rmSync(temporary, { force: true });
    rmSync(output, { force: true });
    console.warn(`[native] ${reason}; using standard ORT fallback`);
    return null;
  };
  if (!['win32', 'linux', 'darwin'].includes(process.platform) || !['x64', 'arm64'].includes(arch)) {
    return fallback(`Unsupported platform ${process.platform}-${arch}`);
  }
  const windows = process.platform === 'win32';
  const candidates = process.env.CXX ? [process.env.CXX] : windows ? ['cl', 'clang-cl'] : ['c++', 'g++', 'clang++'];
  const compiler = candidates.find(command => spawnSync(command,
    windows && /^cl(?:\.exe)?$/i.test(path.basename(command)) ? ['/?'] : ['--version'], { stdio: 'ignore' }).status === 0);
  if (!compiler) return fallback('No C++ compiler found');
  const caches = [
    ...(process.env.npm_config_nodedir ? [process.env.npm_config_nodedir] : []),
    ...(process.env.LOCALAPPDATA ? [path.join(process.env.LOCALAPPDATA, 'node-gyp/Cache', version)] : []),
    path.join(homedir(), '.cache/node-gyp', version),
    path.join(homedir(), '.node-gyp', version),
  ];
  const includeCandidates = [
    process.env.NODE_INCLUDE_DIR,
    ...caches.map(directory => path.join(directory, 'include/node')),
    '/usr/include/node', '/usr/local/include/node',
    '/opt/homebrew/include/node',
    path.resolve(path.dirname(process.execPath), '../include/node'),
    path.join(path.dirname(process.execPath), 'include/node'),
  ].filter((directory): directory is string => !!directory);
  const include = includeCandidates.find(directory => existsSync(path.join(directory, 'node_api.h')));
  if (!include) return fallback('Node headers not found (set NODE_INCLUDE_DIR)');
  const nodeLibrary = windows ? [
    process.env.NODE_LIB_FILE,
    ...[...caches, path.resolve(include, '../..'), path.dirname(process.execPath)]
      .flatMap(directory => [path.join(directory, arch, 'node.lib'), path.join(directory, `win-${arch}/node.lib`), path.join(directory, 'node.lib')]),
  ].find((file): file is string => !!file && existsSync(file)) : undefined;
  if (windows && !nodeLibrary) return fallback('Node import library not found (set NODE_LIB_FILE for the target architecture)');
  mkdirSync(path.dirname(output), { recursive: true });
  const native = path.join(root, 'native/onnx-async');
  mkdirSync(path.join(root, '.tmp'), { recursive: true });
  const buildDirectory = mkdtempSync(path.join(root, '.tmp/native-build-'));
  try {
    const env = { ...process.env, TMPDIR: buildDirectory, ...(windows ? { TEMP: buildDirectory, TMP: buildDirectory } : {}) };
    const target = windows && /clang-cl(?:\.exe)?$/i.test(compiler)
      ? [`--target=${arch === 'arm64' ? 'aarch64' : 'x86_64'}-pc-windows-msvc`]
      : process.platform === 'darwin' ? ['-arch', arch === 'arm64' ? 'arm64' : 'x86_64'] : [];
    const packed: string[] = [];
    {
      const vnni: string[] = [];
      const avx512: string[] = [];
      if (arch === 'x64' && (!windows || /clang-cl(?:\.exe)?$/i.test(compiler))) {
        const include = path.join(root, 'native/ssru/third_party/xsimd/include');
        {
          const object = path.join(buildDirectory, windows ? 'packed-vnni.obj' : 'packed-vnni.o');
          const source = path.join(root, 'native/ssru/packed_vnni.cc');
          // Preserve VNNI kernel in its own TU (no cross-TU LTO); failure keeps exact AVX2.
          const result = spawnSync(compiler, windows ? [
            '/nologo', '/std:c++20', '/O2', '/EHsc', '/MD', '/fp:precise', '/DNOMINMAX',
            ...target, '/clang:-mavx2', '/clang:-mavxvnni', '/clang:-mfma', '/I', include, '/c', source, `/Fo${object}`,
          ] : [
            '-std=c++20', '-O3', '-Wall', '-Wextra', '-fPIC', '-ffp-contract=off',
            ...target, '-mavx2', '-mavxvnni', '-mfma', '-I', include, '-c', source, '-o', object,
          ], { encoding: 'utf8', env });
          if (result.status === 0) vnni.push(object);
          else console.warn(`[native] VNNI compilation unavailable; retaining exact AVX2: ${result.error?.message || result.stderr.trim() || result.stdout.trim()}`);
        }
        {
          const object = path.join(buildDirectory, windows ? 'packed-avx512vnni.obj' : 'packed-avx512vnni.o');
          const source = path.join(root, 'native/ssru/packed_avx512vnni.cc');
          // Keep AVX-512 VNNI strictly isolated in its own TU with runtime CPUID gating.
          const result = spawnSync(compiler, windows ? [
            '/nologo', '/std:c++20', '/O2', '/EHsc', '/MD', '/fp:precise', '/DNOMINMAX',
            ...target, '/clang:-mavx512f', '/clang:-mavx512bw', '/clang:-mavx512dq', '/clang:-mavx512vl', '/clang:-mavx512vnni', '/clang:-mfma',
            '/c', source, `/Fo${object}`,
          ] : [
            '-std=c++20', '-O3', '-Wall', '-Wextra', '-fPIC', '-ffp-contract=off',
            ...target, '-mavx512f', '-mavx512bw', '-mavx512dq', '-mavx512vl', '-mavx512vnni', '-mfma',
            '-c', source, '-o', object,
          ], { encoding: 'utf8', env });
          if (result.status === 0) avx512.push(object);
          else console.warn(`[native] AVX-512 VNNI compilation unavailable: ${result.error?.message || result.stderr.trim() || result.stdout.trim()}`);
        }
      }
      // packed.cc dispatches optional ISA functions itself; keep this object baseline-safe.
      const object = path.join(buildDirectory, windows ? 'packed.obj' : 'packed.o');
      const isaDefines = [
        ...(vnni.length ? [windows ? '/DMTRAN_SSRU_HAVE_VNNI' : '-DMTRAN_SSRU_HAVE_VNNI'] : []),
        ...(avx512.length ? [windows ? '/DMTRAN_SSRU_HAVE_AVX512VNNI' : '-DMTRAN_SSRU_HAVE_AVX512VNNI'] : []),
      ];
      const result = spawnSync(compiler, windows ? [
        '/nologo', '/std:c++20', '/O2', '/EHsc', '/MD', '/utf-8', '/fp:precise', '/DNOMINMAX',
        ...target, ...isaDefines,
        '/c', path.join(root, 'native/ssru/packed.cc'), `/Fo${object}`,
      ] : [
        '-std=c++20', '-O3', '-Wall', '-Wextra', '-fPIC', '-ffp-contract=off',
        ...target, ...isaDefines,
        '-c', path.join(root, 'native/ssru/packed.cc'), '-o', object,
      ], { encoding: 'utf8', env });
      if (result.status === 0) packed.push(windows ? '/DMTRAN_SSRU_PACKED' : '-DMTRAN_SSRU_PACKED', object, ...vnni, ...avx512);
      else console.warn(`[native] Packed compilation unavailable; retaining InvokeOp: ${result.error?.message || result.stderr.trim() || result.stdout.trim()}`);
    }
    const sources = [
      path.join(native, 'addon.cc'),
      path.join(native, 'ssru_op.cc'),
      path.join(root, 'native/ssru/ssru.cc'),
      path.join(root, 'native/ssru/encoder.cc'),
      path.join(root, 'native/ssru/ops.cc'),
      path.join(root, 'native/ssru/pcore_pool.cc'),
    ];
    const result = spawnSync(compiler, windows ? [
      '/nologo', '/std:c++20', '/O2', '/W4', '/EHsc', '/MD', '/LD', '/utf-8', '/fp:precise',
      '/DNOMINMAX', '/DWIN32_LEAN_AND_MEAN', '/DBUILDING_NODE_EXTENSION', '/DMTRAN_NATIVE_SSRU',
      '/I', include, '/I', native,
      ...target, ...sources, ...packed, `/Fo${buildDirectory}${path.sep}`,
      '/link', `/OUT:${temporary}`, `/IMPLIB:${path.join(buildDirectory, 'addon.lib')}`,
      `/MACHINE:${arch === 'arm64' ? 'ARM64' : 'X64'}`, nodeLibrary!, 'delayimp.lib', '/DELAYLOAD:node.exe',
    ] : [
      '-std=c++20', '-O3', '-Wall', '-Wextra', '-shared', '-fPIC', '-pthread', '-ffp-contract=off',
      ...target,
      '-I', include, '-I', native, '-DMTRAN_NATIVE_SSRU', ...sources,
      ...packed,
      ...(process.platform === 'darwin' ? ['-undefined', 'dynamic_lookup'] : ['-ldl']),
      '-o', temporary,
    ], { encoding: 'utf8', env });
    if (result.status !== 0) {
      return fallback(`Compilation failed: ${result.error?.message || result.stderr.trim() || result.stdout.trim() || `exit ${result.status}`}`);
    }
    renameSync(temporary, output);
    console.log(`[native] Built ${path.relative(root, output)}`);
    return output;
  } finally {
    rmSync(buildDirectory, { recursive: true, force: true });
  }
}

if (import.meta.main) buildNative();
