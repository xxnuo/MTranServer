import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { endianness } from 'node:os';
import protobuf from 'protobufjs';

const littleEndian = endianness() === 'LE';

export interface Lexicon { first: number; offsets: number[]; targets: number[] }
export class Shortlist {
  private readonly wordCount: number;
  private readonly baseWords: Uint32Array;
  private readonly scratch: Uint32Array;
  private readonly offsets: Int32Array;
  private readonly targets: Uint32Array;
  private readonly baseCount: number;

  private constructor(private lex: Lexicon, private targetSize: number, private shared: boolean, eos: number, byteIds: number[]) {
    assert(Number.isSafeInteger(targetSize) && targetSize > 1, 'invalid target size');
    assert(Number.isSafeInteger(eos) && eos >= 0 && eos < targetSize && eos !== 1, 'invalid EOS');
    this.wordCount = (targetSize + 31) >>> 5;
    this.baseWords = new Uint32Array(this.wordCount);
    this.scratch = new Uint32Array(this.wordCount);
    this.offsets = Int32Array.from(lex.offsets);
    this.targets = Uint32Array.from(lex.targets);
    let count = 0;
    const addBase = (id: number) => {
      const idx = id >>> 5, bit = 1 << (id & 31);
      if ((this.baseWords[idx] & bit) === 0) { this.baseWords[idx] |= bit; count++; }
    };
    const firstLimit = Math.min(lex.first, targetSize);
    for (let id = 0; id < firstLimit; id++) addBase(id);
    addBase(eos);
    for (const id of byteIds) {
      assert(Number.isSafeInteger(id) && id >= 0 && id < targetSize, 'invalid byte ID');
      addBase(id);
    }
    this.baseCount = count;
  }
  static async load(lexPath: string, sourceVocab: string, targetVocab: string): Promise<Shortlist> {
    const [lexBytes, sourceBytes, targetBytes] = await Promise.all([readFile(lexPath), readFile(sourceVocab), readFile(targetVocab)]);
    const source = vocabularyInfo(sourceBytes), target = vocabularyInfo(targetBytes);
    assert(Number.isSafeInteger(target.eos) && target.eos >= 0 && target.eos < target.size && target.eos !== 1, 'invalid EOS');
    const lex = parseLexicon(lexBytes, target.size, source.size);
    const shared = sourceBytes.length === targetBytes.length && Buffer.compare(sourceBytes, targetBytes) === 0;
    return new Shortlist(lex, target.size, shared, target.eos, target.byteIds);
  }
  private fillBitset(source: ArrayLike<number | bigint>): number {
    assert(source.length > 0, 'invalid source IDs');
    const words = this.scratch;
    words.set(this.baseWords);
    let count = this.baseCount;
    const { offsets, targets, targetSize, shared } = this;
    const maxSource = offsets.length - 1;
    for (let i = 0; i < source.length; i++) {
      const raw = source[i];
      const id = typeof raw === 'bigint' ? Number(raw) : raw;
      assert(Number.isSafeInteger(id) && id >= 0 && id < maxSource, 'invalid source ID');
      if (shared && id < targetSize) {
        const idx = id >>> 5, bit = 1 << (id & 31);
        if ((words[idx] & bit) === 0) { words[idx] |= bit; count++; }
      }
      const start = offsets[id], end = offsets[id + 1];
      for (let j = start; j < end; j++) {
        const tid = targets[j];
        const idx = tid >>> 5, bit = 1 << (tid & 31);
        if ((words[idx] & bit) === 0) { words[idx] |= bit; count++; }
      }
    }
    for (let id = this.lex.first; id < targetSize && (count & 7) !== 0; id++) {
      const idx = id >>> 5, bit = 1 << (id & 31);
      if ((words[idx] & bit) === 0) { words[idx] |= bit; count++; }
    }
    return count;
  }
  candidates(ids: number[]): number[] {
    const count = this.fillBitset(ids);
    const out = new Array<number>(count);
    const words = this.scratch;
    let pos = 0;
    for (let i = 0; i < words.length; i++) {
      let w = words[i];
      const base = i << 5;
      while (w !== 0) {
        const lsb = w & -w;
        out[pos++] = base + (31 - Math.clz32(lsb));
        w ^= lsb;
      }
    }
    return out;
  }
  candidatesBigInt(ids: ArrayLike<number | bigint>): BigInt64Array {
    const count = this.fillBitset(ids);
    const out = new BigInt64Array(count);
    const words = this.scratch;
    let pos = 0;
    if (littleEndian) {
      const i32 = new Int32Array(out.buffer);
      for (let i = 0; i < words.length; i++) {
        let w = words[i];
        const base = i << 5;
        while (w !== 0) {
          const lsb = w & -w;
          i32[(pos++) << 1] = base + (31 - Math.clz32(lsb));
          w ^= lsb;
        }
      }
    } else {
      for (let i = 0; i < words.length; i++) {
        let w = words[i];
        const base = i << 5;
        while (w !== 0) {
          const lsb = w & -w;
          out[pos++] = BigInt(base + (31 - Math.clz32(lsb)));
          w ^= lsb;
        }
      }
    }
    return out;
  }
}
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
  return vocabularyInfo(bytes, expectedSize).byteIds;
}

export function vocabularyInfo(bytes: Uint8Array, expectedSize?: number): { size: number; eos: number; byteIds: number[] } {
  const reader = protobuf.Reader.create(bytes), ids: number[] = [];
  let pieces = 0, eos = 2;
  const tag = (r: protobuf.Reader) => {
    const t = r.uint32();
    assert(t >>> 3 > 0 && [0, 1, 2, 5].includes(t & 7), 'invalid protobuf tag');
    return t;
  };
  const skip = (r: protobuf.Reader, t: number) => { r.skipType(t & 7); assert(r.pos <= r.len, 'truncated protobuf field'); };
  while (reader.pos < reader.len) {
    const t = tag(reader);
    if (t === 18) {
      const trainer = protobuf.Reader.create(reader.bytes());
      while (trainer.pos < trainer.len) {
        const field = tag(trainer);
        if (field === 336) eos = trainer.int32(); else skip(trainer, field);
      }
      continue;
    }
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
  return { size: pieces, eos, byteIds: ids };
}
export function validateRequest(source: number[], candidates: number[], sourceSize: number, targetSize: number, eos: number) {
  assert(Number.isSafeInteger(sourceSize) && sourceSize > 0 && Number.isSafeInteger(targetSize) && targetSize > 1, 'invalid vocab sizes');
  assert(Number.isSafeInteger(eos) && eos >= 0 && eos < targetSize && eos !== 1, 'invalid EOS');
  assert(source.length > 0 && source.every(id => Number.isSafeInteger(id) && id >= 0 && id < sourceSize), 'invalid source IDs');
  assert(candidates.length > 0 && candidates.every((id, i) => Number.isSafeInteger(id) && id >= 0 && id < targetSize && (!i || id > candidates[i - 1])), 'candidates must be sorted unique in-range IDs');
  assert(candidates.includes(eos), 'candidates must include EOS');
}
