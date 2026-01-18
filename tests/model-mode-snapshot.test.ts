import { expect, test } from 'bun:test';
import { fileURLToPath } from 'node:url';

test.each(['default-false', 'default-true', 'explicit-false', 'explicit-true'])('prepareModel snapshots shortlist before registry initialization: %s', async scenario => {
  const script = `
    import { mock } from 'bun:test';
    import assert from 'node:assert/strict';
    import { mkdir, mkdtemp, writeFile, rm } from 'node:fs/promises';
    import path from 'node:path';
    import { setConfig } from './src/config/index.ts';
    const scenario = ${JSON.stringify(scenario)};
    const expected = scenario.endsWith('true');
    await mkdir('.tmp', {recursive:true});
    const root = await mkdtemp(path.resolve('.tmp/model-snapshot-'));
    let releaseRegistry, enteredRegistry, releaseConversion, enteredConversion;
    const registryGate = new Promise(resolve => releaseRegistry = resolve);
    const registryEntered = new Promise(resolve => enteredRegistry = resolve);
    const conversionGate = new Promise(resolve => releaseConversion = resolve);
    const conversionEntered = new Promise(resolve => enteredConversion = resolve);
    let convertedMode;
    const records = ['1.0','2.0'].flatMap(version => ['model','vocab','lex'].map(fileType => ({
      version, fileType, sourceLanguage:'en', targetLanguage:'de',
      attachment:{filename:fileType + '-' + version, location:'fixture'}
    })));
    mock.module('./src/core/factory.ts', () => ({ createDownloader: () => ({ download: async ({outputPath}) => {
      enteredRegistry();
      await registryGate;
      await writeFile(outputPath, JSON.stringify({data:records}));
    } }) }));
    mock.module('./src/converter/cache.ts', () => ({ ensureConverted: async (_root, pair, source, _repair, mode) => {
      assert.equal(pair, 'en_de');
      assert.ok(source.records.every(record => record.version === '1.0'));
      convertedMode = mode;
      enteredConversion();
      await conversionGate;
      return {identity:mode ? 'shortlist' : 'K9'};
    } }));
    setConfig({configDir:root, modelDir:root, enableOfflineMode:false,
      lexicalShortlist:scenario.startsWith('default') ? expected : !expected});
    const { prepareModel } = await import('./src/models/records.ts');
    try {
      const pending = scenario.startsWith('default')
        ? prepareModel('de','en','1.0')
        : prepareModel('de','en','1.0',expected);
      await registryEntered;
      setConfig({lexicalShortlist:!expected});
      releaseRegistry();
      await conversionEntered;
      setConfig({lexicalShortlist:expected});
      releaseConversion();
      assert.deepEqual(await pending, {identity:expected ? 'shortlist' : 'K9'});
      assert.equal(convertedMode, expected);
    } finally { releaseRegistry(); releaseConversion(); await rm(root,{recursive:true,force:true}); }
  `;
  const child = Bun.spawn([process.execPath, '--eval', script], { cwd: fileURLToPath(new URL('..', import.meta.url)), stdout: 'pipe', stderr: 'pipe' });
  const [, stderr, code] = await Promise.all([new Response(child.stdout).text(), new Response(child.stderr).text(), child.exited]);
  expect(code, stderr).toBe(0);
});
