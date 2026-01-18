import assert from 'node:assert/strict';
import protobuf from 'protobufjs';

// Experimental local-file parser, not a production security boundary or hardened loader.
export interface Lexicon { first: number; offsets: number[]; targets: number[] }
export function checksum(bytes: Buffer): bigint {
  let seed = 0n;
  // Marian ignores the final four bytes when the target count is odd.
  for (let i = 0; i + 8 <= bytes.length; i += 8) {
    seed = BigInt.asUintN(64, seed ^ (bytes.readBigUInt64LE(i) + 0x9e3779b9n + (seed << 6n) + (seed >> 2n)));
  }
  return seed;
}
export function parseLexicon(bytes: Buffer, targetSize: number, sourceSize?: number): Lexicon {
  assert(Number.isSafeInteger(targetSize) && targetSize > 1, 'invalid target size');
  assert(bytes.length >= 48, 'short header');
  assert.equal(bytes.readBigUInt64LE(0), 0xF11A48D5013417F5n, 'bad magic');
  const integer = (offset: number) => {
    const n = bytes.readBigUInt64LE(offset);
    assert(n <= BigInt(Number.MAX_SAFE_INTEGER), 'oversized header'); return Number(n);
  };
  const first = integer(16); integer(24);
  const count = integer(32), targetCount = integer(40);
  assert(first <= targetSize, 'invalid frequent prefix');
  if (sourceSize !== undefined) assert(Number.isSafeInteger(sourceSize) && sourceSize > 0 && count === sourceSize + 1, 'source lexicon/vocabulary mismatch');
  const length = 48 + count * 8 + targetCount * 4;
  assert(Number.isSafeInteger(length) && bytes.length === length, 'invalid byte length');
  assert.equal(checksum(bytes.subarray(16)), bytes.readBigUInt64LE(8), 'checksum mismatch');
  const offsets = Array.from({ length: count }, (_, i) => integer(48 + i * 8));
  assert(count >= 2 && offsets[0] === 0 && offsets.at(-1) === targetCount, 'invalid offset endpoints');
  offsets.forEach((n, i) => assert(n <= targetCount && (!i || n >= offsets[i - 1]), 'invalid offset order'));
  const targets = Array.from({ length: targetCount }, (_, i) => bytes.readUInt32LE(48 + count * 8 + i * 4));
  assert(targets.every(id => id < targetSize), 'invalid target ID');
  return { first, offsets, targets };
}
export function generateCandidates(lex: Lexicon, source: number[], targetSize: number, shared: boolean, eos: number, byteIds: number[] = []): number[] {
  assert(Number.isSafeInteger(targetSize) && targetSize > 1, 'invalid target size');
  assert(Number.isSafeInteger(eos) && eos >= 0 && eos < targetSize && eos !== 1, 'invalid EOS');
  const selected = new Set<number>();
  for (let id = 0; id < Math.min(lex.first, targetSize); id++) selected.add(id);
  for (const id of new Set(source)) {
    assert(Number.isSafeInteger(id) && id >= 0 && id + 1 < lex.offsets.length, 'invalid source ID');
    if (shared && id < targetSize) selected.add(id);
    for (let j = lex.offsets[id]; j < lex.offsets[id + 1]; j++) selected.add(lex.targets[j]);
  }
  // Explicit EOS inclusion also covers lexicons whose frequent prefix omits it.
  selected.add(eos);
  for (const id of byteIds) {
    assert(Number.isSafeInteger(id) && id >= 0 && id < targetSize, 'invalid byte ID');
    selected.add(id);
  }
  for (let id = lex.first; id < targetSize && selected.size % 8; id++) selected.add(id);
  return [...selected].sort((a, b) => a - b);
}

export function byteTokenIds(bytes: Uint8Array, expectedSize?: number): number[] {
  const reader = protobuf.Reader.create(bytes), ids: number[] = [];
  let pieces = 0;
  const tag = (r: protobuf.Reader) => {
    const t = r.uint32();
    assert(t >>> 3 > 0 && [0, 1, 2, 5].includes(t & 7), 'invalid protobuf tag');
    return t;
  };
  const skip = (r: protobuf.Reader, t: number) => { r.skipType(t & 7); assert(r.pos <= r.len, 'truncated protobuf field'); };
  while (reader.pos < reader.len) {
    const t = tag(reader);
    if (t !== 10) { skip(reader, t); continue; }
    const piece = protobuf.Reader.create(reader.bytes());
    let type = 1;
    while (piece.pos < piece.len) {
      const p = tag(piece);
      if (p === 24) type = piece.int32(); else skip(piece, p);
    }
    if (type === 6) ids.push(pieces);
    pieces++;
  }
  assert(pieces > 0 && (expectedSize === undefined || pieces === expectedSize), 'SentencePiece vocab size mismatch');
  return ids;
}
export function validateRequest(source: number[], candidates: number[], sourceSize: number, targetSize: number, eos: number) {
  assert(Number.isSafeInteger(sourceSize) && sourceSize > 0 && Number.isSafeInteger(targetSize) && targetSize > 1, 'invalid vocab sizes');
  assert(Number.isSafeInteger(eos) && eos >= 0 && eos < targetSize && eos !== 1, 'invalid EOS');
  assert(source.length > 0 && source.every(id => Number.isSafeInteger(id) && id >= 0 && id < sourceSize), 'invalid source IDs');
  assert(candidates.length > 0 && candidates.every((id, i) => Number.isSafeInteger(id) && id >= 0 && id < targetSize && (!i || id > candidates[i - 1])), 'candidates must be sorted unique in-range IDs');
  assert(candidates.includes(eos), 'candidates must include EOS');
}
