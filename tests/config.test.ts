import { expect, test, spyOn } from 'bun:test';
import fs from 'node:fs';
import fsPromises from 'node:fs/promises';
import { getConfig, resetConfig, setConfig, saveConfigFile } from '../src/config';

test('workers default to auto, preserve explicit counts and reject invalid config values', () => {
  const argv = process.argv;
  const env = process.env.MT_WORKERS_PER_LANGUAGE;
  const read = spyOn(fs, 'readFileSync');
  const write = spyOn(fs, 'writeFileSync').mockImplementation(() => {});
  const mkdir = spyOn(fs, 'mkdirSync').mockImplementation(() => undefined);
  const configure = (file: unknown, environment: string | undefined, flags: string[]) => {
    read.mockReturnValue(JSON.stringify(file));
    process.argv = ['bun', 'fixture', ...flags];
    if (environment === undefined) delete process.env.MT_WORKERS_PER_LANGUAGE;
    else process.env.MT_WORKERS_PER_LANGUAGE = environment;
    resetConfig();
  };
  try {
    for (const [file, environment, flags, expected] of [
      [{}, undefined, [], 0], [{ workersPerLanguage: 1 }, undefined, [], 1],
      [{ workersPerLanguage: 8 }, '1', [], 1],
      [{ workersPerLanguage: 1 }, '4', ['--workers-per-language', '0'], 0],
      [{}, '0', ['--workers-per-language=1'], 1], [{}, undefined, ['--workers-per-language=16'], 16],
    ] as const) {
      configure(file, environment, [...flags]);
      expect(getConfig().workersPerLanguage).toBe(expected);
    }
    for (const value of ['1.5', '-1', 'NaN', 'Infinity', '1junk', '', '9007199254740992', '1=oops']) {
      for (const flags of [['--workers-per-language', value], [`--workers-per-language=${value}`]]) {
        configure({}, undefined, flags);
        expect(() => getConfig()).toThrow('non-negative safe integer');
      }
      configure({}, value, []);
      expect(() => getConfig()).toThrow('non-negative safe integer');
    }
    for (const value of [-1, 1.5, Number.MAX_SAFE_INTEGER + 1, null, '1', true]) {
      configure({ workersPerLanguage: value }, undefined, []);
      expect(() => getConfig()).toThrow('non-negative safe integer');
    }
    configure({}, undefined, []);
    for (const value of [0, 1, 16]) {
      setConfig({ workersPerLanguage: value });
      expect(getConfig().workersPerLanguage).toBe(value);
      saveConfigFile({ workersPerLanguage: value });
    }
    for (const value of [-1, 1.5, NaN, Infinity, Number.MAX_SAFE_INTEGER + 1]) {
      expect(() => setConfig({ workersPerLanguage: value })).toThrow('non-negative safe integer');
      expect(() => saveConfigFile({ workersPerLanguage: value })).toThrow('non-negative safe integer');
      expect(getConfig().workersPerLanguage).toBe(16);
    }
  } finally {
    read.mockRestore(); write.mockRestore(); mkdir.mockRestore();
    process.argv = argv;
    if (env === undefined) delete process.env.MT_WORKERS_PER_LANGUAGE;
    else process.env.MT_WORKERS_PER_LANGUAGE = env;
    resetConfig();
  }
});

test('desktop workers default to auto and validate explicit counts before persistence', async () => {
  const { getDefaultDesktopConfig, saveDesktopConfig } = await import('../desktop/config.js');
  const mkdir = spyOn(fsPromises, 'mkdir').mockResolvedValue(undefined);
  const write = spyOn(fsPromises, 'writeFile').mockResolvedValue(undefined);
  const unlink = spyOn(fsPromises, 'unlink').mockResolvedValue(undefined);
  try {
    expect(getDefaultDesktopConfig().server.workersPerLanguage).toBe(0);
    for (const value of [0, 1, 16, '1']) {
      expect((await saveDesktopConfig({ server: { workersPerLanguage: value } })).server.workersPerLanguage).toBe(Number(value));
    }
    for (const value of [-1, 1.5, NaN, Infinity, Number.MAX_SAFE_INTEGER + 1, '1.5', '1junk', '', null, true]) {
      await expect(saveDesktopConfig({ server: { workersPerLanguage: value } })).rejects.toThrow('non-negative safe integer');
    }
  } finally { mkdir.mockRestore(); write.mockRestore(); unlink.mockRestore(); }
});

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
