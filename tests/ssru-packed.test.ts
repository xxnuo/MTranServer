import assert from 'node:assert/strict';
import { test } from 'bun:test';
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const root = path.resolve(import.meta.dir, '..');
const supported = ['linux', 'darwin', 'win32'].includes(process.platform) && ['x64', 'arm64'].includes(process.arch);

test.skipIf(!supported)('packed SSRU exact and ORT-saturating goldens with baseline CPU/OS gating', () => {
  const compiler = (process.env.CXX ? [process.env.CXX] : process.platform === 'win32' ? ['cl', 'clang-cl', 'clang++'] : ['c++', 'g++', 'clang++'])
    .find(cxx => spawnSync(cxx, [/^(cl|clang-cl)(\.exe)?$/i.test(path.basename(cxx)) ? '/?' : '--version'], { stdio: 'ignore' }).status === 0);
  if (!compiler) { console.warn('Packed test skipped: C++ compiler unavailable'); return; }
  mkdirSync(path.join(root, '.tmp'), { recursive: true });
  const directory = mkdtempSync(path.join(root, '.tmp/ssru-packed-test-'));
  const msvc = /^(cl|clang-cl)(\.exe)?$/i.test(path.basename(compiler));
  try {
    const run = (args: string[]) => {
      const result = spawnSync(compiler, args, { cwd: directory, encoding: 'utf8', env: { ...process.env, TMPDIR: directory } });
      assert.equal(result.status, 0, result.error?.message || result.stderr + result.stdout);
    };
    const common = msvc ? ['/nologo', '/std:c++20', '/O2', '/W4', '/EHsc', '/fp:precise']
      : ['-std=c++20', '-O2', '-Wall', '-Wextra', '-ffp-contract=off'];
    const vnni: string[] = [];
    if (!msvc && process.arch === 'x64') {
      const probe = path.join(directory, 'probe.cc');
      writeFileSync(probe, '');
      if (spawnSync(compiler, ['-mavx2', '-mavxvnni', '-c', probe, '-o', path.join(directory, 'probe.o')], { stdio: 'ignore' }).status === 0) {
        const object = path.join(directory, 'vnni.o');
        run([...common, '-mavx2', '-mavxvnni', '-I', path.join(root, 'native/ssru/third_party/xsimd/include'),
          '-c', path.join(root, 'native/ssru/packed_vnni.cc'), '-o', object]);
        vnni.push(object);
      }
    }
    const modes = process.arch === 'x64' ? ['auto', 'no-vnni', 'exact-avx2', 'ort-avx2'] : ['auto', 'no-vnni'];
    let hostArithmetic: string | undefined;
    for (const mode of modes) {
      const disableVnni = mode !== 'auto';
      const object = path.join(directory, msvc ? 'packed.obj' : 'packed.o');
      const binary = path.join(directory, process.platform === 'win32' ? 'test.exe' : 'test');
      const define = disableVnni ? [msvc ? '/DMTRAN_SSRU_NO_VNNI' : '-DMTRAN_SSRU_NO_VNNI',
        msvc ? '/DMTRAN_SSRU_NO_DOTPROD' : '-DMTRAN_SSRU_NO_DOTPROD'] : [];
      if (vnni.length) define.push('-DMTRAN_SSRU_HAVE_VNNI');
      // Explicit arithmetic tests; NO_VNNI alone must retain the host's ORT semantics.
      if (mode === 'exact-avx2') define.push(msvc ? '/DMTRAN_SSRU_TEST_AVX2_EXACT' : '-DMTRAN_SSRU_TEST_AVX2_EXACT');
      if (mode === 'ort-avx2') define.push(msvc ? '/DMTRAN_SSRU_TEST_AVX2_ORT' : '-DMTRAN_SSRU_TEST_AVX2_ORT');
      const source = path.join(root, 'native/ssru/packed.cc');
      run(msvc ? [...common, ...define, '/c', source, `/Fo${object}`] : [...common, ...define, '-c', source, '-o', object]);
      const main = path.join(root, 'native/ssru/packed_test.cc');
      // Neither object is globally compiled for an optional ISA; unsupported hosts must run safely.
      run(msvc ? [...common, main, object, `/Fe${binary}`] : [...common, '-pthread', main, object, ...vnni, '-o', binary]);
      const result = spawnSync(binary, [], { encoding: 'utf8' });
      assert.equal(result.status, 0, result.error?.message || result.stderr + result.stdout);
      assert.match(result.stdout, /PASS: scalar golden|SKIP: packed CPU\/OS unavailable/);
      if (disableVnni) assert.doesNotMatch(result.stdout, /Backend: packed-avxvnni/);
      if (disableVnni) assert.doesNotMatch(result.stdout, /Backend: packed-sdot/);
      if (!result.stdout.includes('SKIP:')) {
        const arithmetic = /scalar golden \(([^)]+)\)/.exec(result.stdout)?.[1];
        assert(arithmetic);
        if (mode === 'auto') hostArithmetic = arithmetic;
        if (mode === 'no-vnni') assert.equal(arithmetic, hostArithmetic, 'disabling VNNI must preserve host ORT arithmetic');
        if (mode === 'exact-avx2') assert.match(result.stdout, /Backend: packed-avx2\r?\n[\s\S]*mathematically exact/);
        if (mode === 'ort-avx2') assert.match(result.stdout, /Backend: packed-avx2-ort\r?\n[\s\S]*ORT int16 saturation/);
      }
    }
  } finally { rmSync(directory, { recursive: true, force: true }); }
}, 60000);
