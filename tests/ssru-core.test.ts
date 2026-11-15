import assert from 'node:assert/strict';
import { test } from 'bun:test';
import { spawnSync } from 'node:child_process';
import { mkdirSync, mkdtempSync, rmSync } from 'node:fs';
import path from 'node:path';

const root = path.resolve(import.meta.dir, '..');
const supported = ['linux', 'darwin'].includes(process.platform) && ['x64', 'arm64'].includes(process.arch);
const compiler = supported ? (process.env.CXX ? [process.env.CXX] : ['c++', 'g++', 'clang++'])
  .find(command => spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0) : undefined;
const skipReason = !supported ? `unsupported ${process.platform}-${process.arch}` : !compiler ? 'C++ compiler unavailable' : '';
if (skipReason) console.warn(`SSRU core tests skipped: ${skipReason}`);

test.skipIf(!!skipReason)('SSRU core scalar reference, boundaries, recovery, allocations and concurrency', async () => {
  mkdirSync(path.join(root, '.tmp'), { recursive: true });
  const directory = mkdtempSync(path.join(root, '.tmp/ssru-core-test-'));
  const binary = path.join(directory, 'ssru-test');
  const run = async (command: string[]) => {
    const child = Bun.spawn(command, { cwd: root, stdout: 'pipe', stderr: 'pipe' });
    const timer = setTimeout(() => child.kill(), 60_000);
    try {
      const [out, err, exit] = await Promise.all([new Response(child.stdout).text(), new Response(child.stderr).text(), child.exited]);
      assert.equal(exit, 0, out + err);
      return out;
    } finally { clearTimeout(timer); }
  };
  try {
    await run([compiler!, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-ffp-contract=off', '-pthread',
      path.join(root, 'native/ssru/ssru.cc'), path.join(root, 'native/ssru/ssru_test.cc'), '-o', binary]);
    assert.match(await run([binary]), /PASS: scalar stage reference/);
  } finally { rmSync(directory, { recursive: true, force: true }); }
}, 130_000);
