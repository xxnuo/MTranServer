import { expect, test } from 'bun:test';
import { quantize4, roundEven } from '../src/converter/graph';
import { readRelease } from '../src/converter/release';
import { mkdtemp, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';

test('INT8 round-to-even differs from JavaScript round at half steps', () => {
  expect([-2.5,-1.5,-.5,.5,1.5,2.5].map(roundEven)).toEqual([-2,-2,0,0,2,2]);
});
test('INT4 packs adjacent columns with signed extremum and zero padding', () => {
  const { packed, scales } = quantize4(Float32Array.from([-8, 7, 0]), 3, 1);
  expect(scales[0]).toBe(1); expect(packed[0]).toBe(0xf0); expect(packed[1]).toBe(0x88);
  expect(packed.length).toBe(32);
});
test('release parser rejects truncated and oversized inputs before allocating tensors', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'mtran-parser-'));
  try {
    const file = path.join(root, 'model'); await writeFile(file, Buffer.alloc(8));
    await expect(readRelease(file)).rejects.toThrow('Truncated');
    const header = Buffer.alloc(16); header.writeBigUInt64LE(1n); header.writeBigUInt64LE(0xffffffffffffffffn, 8);
    await writeFile(file, header); await expect(readRelease(file)).rejects.toThrow('Oversized');
  } finally { await rm(root, {recursive:true,force:true}); }
});
