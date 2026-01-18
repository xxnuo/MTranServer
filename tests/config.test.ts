import { expect, test, spyOn } from 'bun:test';
import fs from 'node:fs';
import { getConfig, resetConfig, setConfig, saveConfigFile } from '../src/config';

test('lexical shortlist defaults off and CLI overrides environment overrides file for true and false', () => {
  const argv = process.argv;
  const env = process.env.MT_LEXICAL_SHORTLIST;
  const fastEnv = process.env.MT_FAST;
  delete process.env.MT_FAST;
  const read = spyOn(fs, 'readFileSync');
  try {
    for (const [file, environment, flags, expected] of [
      [{}, undefined, [], false],
      [{ lexicalShortlist: true }, undefined, [], true],
      [{ lexicalShortlist: false }, undefined, [], false],
      [{ lexicalShortlist: true }, 'false', [], false],
      [{ lexicalShortlist: false }, 'true', [], true],
      [{}, 'true', ['--no-lexical-shortlist'], false],
      [{}, 'false', ['--lexical-shortlist'], true],
      [{}, 'true', ['--lexical-shortlist=false'], false],
      [{}, 'false', ['--lexical-shortlist=true'], true],
      [{}, '1', [], true],
      [{}, '0', [], false],
      [{ lexicalShortlist: 'false' }, undefined, [], false],
    ] as const) {
      read.mockReturnValue(JSON.stringify(file));
      process.argv = ['bun', 'fixture', ...flags];
      if (environment === undefined) delete process.env.MT_LEXICAL_SHORTLIST;
      else process.env.MT_LEXICAL_SHORTLIST = environment;
      resetConfig();
      expect(getConfig().lexicalShortlist).toBe(expected);
    }
  } finally {
    read.mockRestore();
    process.argv = argv;
    if (env === undefined) delete process.env.MT_LEXICAL_SHORTLIST;
    else process.env.MT_LEXICAL_SHORTLIST = env;
    if (fastEnv === undefined) delete process.env.MT_FAST;
    else process.env.MT_FAST = fastEnv;
    resetConfig();
  }
});

test('fast aliases obey CLI > environment > file, canonical name wins within each layer', () => {
  const argv = process.argv;
  const env = { fast: process.env.MT_FAST, legacy: process.env.MT_LEXICAL_SHORTLIST };
  const read = spyOn(fs, 'readFileSync');
  try {
    for (const [file, fastEnv, legacyEnv, flags, expected] of [
      [{}, undefined, undefined, [], false],
      [{ fast: true }, undefined, undefined, [], true],
      [{ fast: false, lexicalShortlist: true }, undefined, undefined, [], false],
      [{ fast: true, lexicalShortlist: false }, undefined, undefined, [], true],
      [{ fast: true }, 'false', 'true', [], false],
      [{ fast: false }, 'true', 'false', [], true],
      [{ fast: false }, undefined, 'true', [], true],
      [{ fast: true }, undefined, 'false', [], false],
      [{ fast: false }, 'false', undefined, ['--fast'], true],
      [{ fast: true }, 'true', undefined, ['--no-fast'], false],
      [{ fast: true }, 'true', undefined, ['--fast=false'], false],
      [{}, undefined, undefined, ['--fast', 'false'], false],
      [{}, undefined, undefined, ['--fast=true'], true],
      [{}, undefined, undefined, ['--fast=1'], true],
      [{}, undefined, undefined, ['--fast=0'], false],
      [{}, '1', 'false', [], true],
      [{}, '0', 'true', [], false],
      [{}, 'true', undefined, ['--no-lexical-shortlist'], false],
      [{}, 'false', undefined, ['--lexical-shortlist'], true],
      [{}, undefined, 'true', ['--no-fast'], false],
      [{}, undefined, 'false', ['--fast'], true],
      [{}, undefined, undefined, ['--lexical-shortlist', '--no-fast'], false],
      [{}, undefined, undefined, ['--no-lexical-shortlist', '--fast'], true],
      [{}, undefined, undefined, ['--fast=false', '--lexical-shortlist'], false],
    ] as const) {
      read.mockReturnValue(JSON.stringify(file));
      process.argv = ['bun', 'fixture', ...flags];
      if (fastEnv === undefined) delete process.env.MT_FAST;
      else process.env.MT_FAST = fastEnv;
      if (legacyEnv === undefined) delete process.env.MT_LEXICAL_SHORTLIST;
      else process.env.MT_LEXICAL_SHORTLIST = legacyEnv;
      resetConfig();
      expect(getConfig().fast).toBe(expected);
      expect(getConfig().lexicalShortlist).toBe(expected);
    }
  } finally {
    read.mockRestore();
    process.argv = argv;
    if (env.fast === undefined) delete process.env.MT_FAST;
    else process.env.MT_FAST = env.fast;
    if (env.legacy === undefined) delete process.env.MT_LEXICAL_SHORTLIST;
    else process.env.MT_LEXICAL_SHORTLIST = env.legacy;
    resetConfig();
  }
});

test('fast and lexicalShortlist share state through mutation, setConfig and file persistence', () => {
  const argv = process.argv;
  const env = { fast: process.env.MT_FAST, legacy: process.env.MT_LEXICAL_SHORTLIST };
  let contents = '{}';
  const read = spyOn(fs, 'readFileSync').mockImplementation(() => contents);
  const mkdir = spyOn(fs, 'mkdirSync').mockImplementation(() => undefined);
  const write = spyOn(fs, 'writeFileSync').mockImplementation((_file, data) => { contents = String(data); });
  process.argv = ['bun', 'fixture'];
  delete process.env.MT_FAST;
  delete process.env.MT_LEXICAL_SHORTLIST;
  resetConfig();
  try {
    for (const patch of [
      { fast: true }, { lexicalShortlist: false },
      { fast: true, lexicalShortlist: false }, { fast: false, lexicalShortlist: true },
    ]) {
      const expected = patch.fast ?? patch.lexicalShortlist;
      setConfig(patch);
      expect(getConfig().fast).toBe(expected);
      expect(getConfig().lexicalShortlist).toBe(expected);
      setConfig({ port: '9000' });
      expect(getConfig().lexicalShortlist).toBe(expected);
      // Save against a conflicting current value to catch merge-order regressions.
      setConfig({ fast: !expected });
      saveConfigFile(patch);
      expect(JSON.parse(contents)).toMatchObject({ fast: expected, lexicalShortlist: expected });
      resetConfig();
      expect(getConfig().fast).toBe(expected);
      expect(getConfig().lexicalShortlist).toBe(expected);
    }
    getConfig().fast = true;
    expect(getConfig().lexicalShortlist).toBe(true);
    getConfig().lexicalShortlist = false;
    expect(getConfig().fast).toBe(false);
  } finally {
    read.mockRestore(); mkdir.mockRestore(); write.mockRestore();
    process.argv = argv;
    if (env.fast === undefined) delete process.env.MT_FAST;
    else process.env.MT_FAST = env.fast;
    if (env.legacy === undefined) delete process.env.MT_LEXICAL_SHORTLIST;
    else process.env.MT_LEXICAL_SHORTLIST = env.legacy;
    resetConfig();
  }
});
