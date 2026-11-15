import assert from 'node:assert/strict';
import { test } from 'bun:test';
import { mkdtempSync, mkdirSync, rmSync } from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const root = path.resolve(import.meta.dir, '..');
const supported = ['linux', 'darwin'].includes(process.platform) && process.arch === 'x64';

test.skipIf(!supported)('packed SSRU uses isolated AVXVNNI code and a safe baseline host gate', () => {
  const compiler = (process.env.CXX ? [process.env.CXX] : ['c++', 'g++', 'clang++'])
    .find(cxx => spawnSync(cxx, ['--version'], { stdio: 'ignore' }).status === 0);
  if (!compiler) { console.warn('Packed test skipped: C++ compiler unavailable'); return; }
  mkdirSync(path.join(root, '.tmp'), { recursive: true });
  const directory = mkdtempSync(path.join(root, '.tmp/ssru-packed-test-'));
  try {
    const run = (args: string[]) => spawnSync(compiler, args, { cwd: root, encoding: 'utf8', env: { ...process.env, TMPDIR: directory } });
    // Probe the flag separately: a supported compiler must not hide source failures.
    const feature = run(['-mavx2', '-mavxvnni', '-x', 'c++', '-c', '/dev/null', '-o', path.join(directory, 'probe.o')]);
    if (feature.status !== 0) { console.warn('Packed test skipped: compiler lacks AVXVNNI'); return; }
    const common = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-ffp-contract=off'];
    const object = path.join(directory, 'packed.o');
    const packed = run([...common, '-mavx2', '-mavxvnni', '-I', 'native/ssru/third_party/xsimd/include', '-c', 'native/ssru/packed.cc', '-o', object]);
    assert.equal(packed.status, 0, packed.stderr);
    // No AVX flags or xsimd include directory leak into the entry point.
    const main = run([...common, '-pthread', 'native/ssru/packed_test.cc', object, '-o', path.join(directory, 'test')]);
    assert.equal(main.status, 0, main.stderr);
    const result = spawnSync(path.join(directory, 'test'), [], { encoding: 'utf8' });
    assert.equal(result.status, 0, result.stderr + result.stdout);
    assert.match(result.stdout, /PASS: scalar golden|SKIP: AVXVNNI unavailable/);
  } finally { rmSync(directory, { recursive: true, force: true }); }
}, 30000);
