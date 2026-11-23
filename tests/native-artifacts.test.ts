import assert from 'node:assert/strict';
import { test } from 'bun:test';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { collectNative, nativeTargets } from '../scripts/collect-native';
import { files, source, version } from '../scripts/prepare-ort-macos-x64';
import { nativeInputHashes } from '../scripts/native-inputs';

test('publishing refuses an incomplete release before cleaning the current build', () => {
  const environment = { ...process.env };
  delete environment.MTRAN_NATIVE_ARTIFACT_DIR;
  const result = spawnSync(process.execPath, ['scripts/build.ts', '--lib', '--publish'], { env: environment, encoding: 'utf8' });
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /Publishing requires MTRAN_NATIVE_ARTIFACT_DIR/);
  assert.doesNotMatch(result.stdout, /Cleaning dist/);
});

test('release assembly requires matching binary hashes and all six passing platform reports', () => {
  mkdirSync('.tmp', { recursive: true });
  const directory = mkdtempSync(path.resolve('.tmp/native-artifacts-'));
  try {
    const ort = path.join(directory, 'ort/darwin/x64');
    mkdirSync(ort, { recursive: true });
    const bytes = Buffer.alloc(8);
    bytes.writeUInt32LE(0xfeedfacf); bytes.writeUInt32LE(0x01000007, 4);
    const hashes: Record<string, string> = {};
    // Synthetic headers exercise package integrity only; real inference runs on target CI.
    for (const name of [...files, 'LICENSE', 'ThirdPartyNotices.txt']) {
      writeFileSync(path.join(ort, name), bytes);
      hashes[name] = createHash('sha256').update(bytes).digest('hex');
    }
    writeFileSync(path.join(ort, 'provenance.json'), JSON.stringify({ version, runtimeVersion: version, platform: 'darwin', arch: 'x64', source, files: hashes }));
    for (const target of nativeTargets) {
      const bytes = Buffer.from(target);
      writeFileSync(path.join(directory, `onnx-async-${target}.node`), bytes);
      const [platform, arch] = target.split('-');
      writeFileSync(path.join(directory, `${target}.json`), JSON.stringify({ platform, arch, ort: '1.30.0',
        addon_sha256: createHash('sha256').update(bytes).digest('hex'),
        source_sha256: nativeInputHashes(),
        results: [0, 1, 2, 3].map(i => ({ mode: i % 2 ? 'standard' : 'fast', samples: 1024, token_exact: 1024, flores_devtest_samples: 1012,
          corpus_exact_rate: 1, corpus_chrf: 1, corpus_len_ratio: 1,
          flores_exact_rate: 1, flores_chrf: 1, flores_len_ratio: 1, differences: [] })) }));
      for (const host of ['node-http', 'electron-http', ...(platform === 'linux' ? ['electron-http-2', 'electron-http-3'] : [])])
        writeFileSync(path.join(directory, `${target}-${host}.json`), JSON.stringify({ platform, arch, status: 'passed',
          addon_sha256: createHash('sha256').update(bytes).digest('hex'),
          scenarios: ['default', 'disabled', 'missing-addon'].map(name => ({ name, status: 'passed' })) }));
    }
    const output = path.join(directory, 'dist');
    collectNative(directory, output);
    for (const target of nativeTargets) assert.equal(readFileSync(path.join(output, `onnx-async-${target}.node`), 'utf8'), target);
    const reportPath = path.join(directory, 'win32-arm64.json');
    const report = JSON.parse(readFileSync(reportPath, 'utf8'));
    report.results[0].token_exact--;
    writeFileSync(reportPath, JSON.stringify(report));
    assert.throws(() => collectNative(directory, output), /quality/);
    report.results[0].token_exact++;
    report.results[0].flores_chrf = 0.95;
    writeFileSync(reportPath, JSON.stringify(report));
    assert.throws(() => collectNative(directory, output), /quality/);
    Object.assign(report.results[0], {
      corpus_exact_rate: 0.85,
      corpus_chrf: 0.98,
      corpus_len_ratio: 1,
      flores_exact_rate: 0.82,
      flores_chrf: 0.97,
      flores_len_ratio: 1,
      token_exact: 840,
      differences: Array(184).fill({}),
    });
    writeFileSync(reportPath, JSON.stringify(report));
    collectNative(directory, output);
    writeFileSync(path.join(directory, 'onnx-async-win32-arm64.node'), 'changed');
    assert.throws(() => collectNative(directory, output), /Unverified binary/);
  } finally { rmSync(directory, { recursive: true, force: true }); }
});
