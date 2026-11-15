import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { copyFileSync, cpSync, mkdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { verifyBundle } from './prepare-ort-macos-x64';
import { nativeInputHashes } from './native-inputs';

export const nativeTargets = ['linux-x64', 'linux-arm64', 'win32-x64', 'win32-arm64', 'darwin-x64', 'darwin-arm64'];

// Artifacts live outside dist so prepublishOnly can rebuild JS without deleting them.
export function collectNative(directory: string, output: string) {
  const expectedSources = nativeInputHashes();
  for (const target of nativeTargets) {
    const filename = `onnx-async-${target}.node`;
    const source = path.join(directory, filename);
    const report = JSON.parse(readFileSync(path.join(directory, `${target}.json`), 'utf8'));
    assert.equal(`${report.platform}-${report.arch}`, target, `Wrong target: ${target}`);
    assert.equal(createHash('sha256').update(readFileSync(source)).digest('hex'), report.addon_sha256, `Unverified binary: ${target}`);
    assert.equal(report.ort, '1.30.0', `ORT version mismatch: ${target}`);
    assert.deepEqual(report.source_sha256, expectedSources, `Stale platform artifact: ${target}`);
    assert(report.results?.length === 2 && report.results.every((r: { samples: number; token_exact: number; flores_devtest_samples: number; differences: unknown[] }) =>
      r.flores_devtest_samples === 1012 && r.samples >= 1012 && r.token_exact === r.samples && r.differences.length === 0), `Unresolved quality differences: ${target}`);
    for (const host of ['node-http', 'electron-http', ...(target.startsWith('linux-') ? ['electron-http-2', 'electron-http-3'] : [])]) {
      const http = JSON.parse(readFileSync(path.join(directory, `${target}-${host}.json`), 'utf8'));
      assert.equal(http.status, 'passed', `HTTP verification failed: ${target}/${host}`);
      assert.equal(http.addon_sha256, report.addon_sha256, `Wrong HTTP addon: ${target}/${host}`);
      assert.equal(`${http.platform}-${http.arch}`, target, `Wrong HTTP platform: ${target}/${host}`);
      assert.deepEqual(http.scenarios?.map((scenario: { name: string; status: string }) => [scenario.name, scenario.status]),
        [['default', 'passed'], ['disabled', 'passed'], ['missing-addon', 'passed']], `Incomplete HTTP verification: ${target}/${host}`);
    }
    mkdirSync(output, { recursive: true });
    copyFileSync(source, path.join(output, filename));
  }
  const ort = path.join(directory, 'ort/darwin/x64');
  verifyBundle(ort);
  cpSync(ort, path.join(output, 'ort/darwin/x64'), { recursive: true });
}
