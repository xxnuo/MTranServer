import { expect, test, spyOn } from 'bun:test';
import fs from 'node:fs';
import { getConfig, resetConfig } from '../src/config';

test('lexical shortlist defaults off and CLI overrides environment overrides file for true and false', () => {
  const argv = process.argv;
  const env = process.env.MT_LEXICAL_SHORTLIST;
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
    resetConfig();
  }
});
