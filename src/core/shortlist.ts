import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { endianness } from 'node:os';
import protobuf from 'protobufjs';

const littleEndian = endianness() === 'LE';

function sortInt32(arr: Int32Array, left: number, right: number): void {
  while (left < right) {
    if (right - left <= 16) {
      for (let i = left + 1; i <= right; i++) {
        const v = arr[i];
        let j = i - 1;
        while (j >= left && arr[j] > v) {
          arr[j + 1] = arr[j];
          j--;
        }
        arr[j + 1] = v;
      }
      return;
    }
    const mid = (left + right) >>> 1;
    const pivot = arr[mid];
    let i = left, j = right;
    while (i <= j) {
      while (arr[i] < pivot) i++;
      while (arr[j] > pivot) j--;
      if (i <= j) {
        const tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
        i++; j--;
      }
    }
    if (j - left < right - i) {
      if (left < j) sortInt32(arr, left, j);
      left = i;
    } else {
      if (i < right) sortInt32(arr, i, right);
      right = j;
    }
  }
}

export interface Lexicon { first: number; offsets: number[]; targets: number[] }
export class Shortlist {
  private readonly wordCount: number;
  private readonly baseWords: Uint32Array;
  private readonly extraWords: Uint32Array;
  private readonly dirtyWords: Int32Array;
  private readonly baseIds: Int32Array;
  private readonly extraIds: Int32Array;
  private readonly basePrefixLen: number;
  private readonly extraWordStart: number;
  private readonly offsets: Int32Array;
  private readonly targets: Uint32Array;
  private readonly baseCount: number;

  private constructor(private lex: Lexicon, private targetSize: number, private shared: boolean, eos: number, byteIds: number[]) {
    assert(Number.isSafeInteger(targetSize) && targetSize > 1, 'invalid target size');
    assert(Number.isSafeInteger(eos) && eos >= 0 && eos < targetSize && eos !== 1, 'invalid EOS');
    this.wordCount = (targetSize + 31) >>> 5;
    this.baseWords = new Uint32Array(this.wordCount);
    this.extraWords = new Uint32Array(this.wordCount);
    this.dirtyWords = new Int32Array(this.wordCount);
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
    const baseIds = new Int32Array(count);
    let bi = 0;
    for (let i = 0; i < this.baseWords.length; i++) {
      let w = this.baseWords[i];
      if (w === 0) continue;
      const base = i << 5;
      while (w !== 0) {
        const lsb = w & -w;
        baseIds[bi++] = base + (31 - Math.clz32(lsb));
        w ^= lsb;
      }
    }
    let prefix = 0;
    while (prefix < baseIds.length && baseIds[prefix] === prefix) prefix++;
    this.baseIds = baseIds;
    this.basePrefixLen = prefix;
    this.extraWordStart = prefix >>> 5;
    this.extraIds = new Int32Array(targetSize - count);
  }
  static async load(lexPath: string, sourceVocab: string, targetVocab: string): Promise<Shortlist> {
    const [lexBytes, sourceBytes, targetBytes] = await Promise.all([readFile(lexPath), readFile(sourceVocab), readFile(targetVocab)]);
    const source = vocabularyInfo(sourceBytes), target = vocabularyInfo(targetBytes);
    assert(Number.isSafeInteger(target.eos) && target.eos >= 0 && target.eos < target.size && target.eos !== 1, 'invalid EOS');
    const lex = parseLexicon(lexBytes, target.size, source.size);
    const shared = sourceBytes.length === targetBytes.length && Buffer.compare(sourceBytes, targetBytes) === 0;
    return new Shortlist(lex, target.size, shared, target.eos, target.byteIds);
  }
  private fillExtra(source: ArrayLike<number | bigint>): number {
    assert(source.length > 0, 'invalid source IDs');
    const { baseWords, extraWords, dirtyWords, offsets, targets, targetSize, shared } = this;
    let extraCount = 0;
    let dirtyCount = 0;
    let minWord = this.wordCount;
    let maxWord = -1;
    const maxSource = offsets.length - 1;
    try {
      for (let i = 0; i < source.length; i++) {
        const raw = source[i];
        const id = typeof raw === 'bigint' ? Number(raw) : raw;
        assert(Number.isSafeInteger(id) && id >= 0 && id < maxSource, 'invalid source ID');
        if (shared && id < targetSize) {
          const idx = id >>> 5, bit = 1 << (id & 31);
          const cur = extraWords[idx];
          if (((baseWords[idx] | cur) & bit) === 0) {
            if (cur === 0) {
              dirtyWords[dirtyCount++] = idx;
              if (idx < minWord) minWord = idx;
              if (idx > maxWord) maxWord = idx;
            }
            extraWords[idx] = cur | bit;
            extraCount++;
          }
        }
        const start = offsets[id], end = offsets[id + 1];
        for (let j = start; j < end; j++) {
          const tid = targets[j];
          const idx = tid >>> 5, bit = 1 << (tid & 31);
          const cur = extraWords[idx];
          if (((baseWords[idx] | cur) & bit) === 0) {
            if (cur === 0) {
              dirtyWords[dirtyCount++] = idx;
              if (idx < minWord) minWord = idx;
              if (idx > maxWord) maxWord = idx;
            }
            extraWords[idx] = cur | bit;
            extraCount++;
          }
        }
      }
      let totalCount = this.baseCount + extraCount;
      for (let id = this.lex.first; id < targetSize && (totalCount & 7) !== 0; id++) {
        const idx = id >>> 5, bit = 1 << (id & 31);
        const cur = extraWords[idx];
        if (((baseWords[idx] | cur) & bit) === 0) {
          if (cur === 0) {
            dirtyWords[dirtyCount++] = idx;
            if (idx < minWord) minWord = idx;
            if (idx > maxWord) maxWord = idx;
          }
          extraWords[idx] = cur | bit;
          totalCount++;
          extraCount++;
        }
      }
    } catch (err) {
      for (let k = 0; k < dirtyCount; k++) extraWords[dirtyWords[k]] = 0;
      throw err;
    }
    if (dirtyCount === 0) return 0;
    const extraIds = this.extraIds;
    let e = 0;
    if (dirtyCount * 6 <= maxWord - minWord + 1) {
      sortInt32(dirtyWords, 0, dirtyCount - 1);
      for (let k = 0; k < dirtyCount; k++) {
        const idx = dirtyWords[k];
        let w = extraWords[idx];
        extraWords[idx] = 0;
        const base = idx << 5;
        while (w !== 0) {
          const lsb = w & -w;
          extraIds[e++] = base + (31 - Math.clz32(lsb));
          w ^= lsb;
        }
      }
    } else {
      for (let i = minWord; i <= maxWord; i++) {
        let w = extraWords[i];
        if (w === 0) continue;
        extraWords[i] = 0;
        const base = i << 5;
        while (w !== 0) {
          const lsb = w & -w;
          extraIds[e++] = base + (31 - Math.clz32(lsb));
          w ^= lsb;
        }
      }
    }
    return extraCount;
  }
  candidates(ids: number[]): number[] {
    const extraCount = this.fillExtra(ids);
    const { baseIds, basePrefixLen, baseCount, extraIds } = this;
    const out = new Array<number>(baseCount + extraCount);
    let pos = 0;
    for (; pos < basePrefixLen; pos++) out[pos] = pos;
    let bi = basePrefixLen, ei = 0;
    while (bi < baseCount && ei < extraCount) {
      const b = baseIds[bi], e = extraIds[ei];
      if (b < e) { out[pos++] = b; bi++; }
      else { out[pos++] = e; ei++; }
    }
    while (bi < baseCount) out[pos++] = baseIds[bi++];
    while (ei < extraCount) out[pos++] = extraIds[ei++];
    return out;
  }
  candidatesBigInt(ids: ArrayLike<number | bigint>): BigInt64Array {
    const extraCount = this.fillExtra(ids);
    const { baseIds, basePrefixLen, baseCount, extraIds } = this;
    const out = new BigInt64Array(baseCount + extraCount);
    let pos = 0;
    if (littleEndian) {
      const i32 = new Int32Array(out.buffer);
      for (; pos < basePrefixLen; pos++) i32[pos << 1] = pos;
      let bi = basePrefixLen, ei = 0;
      while (bi < baseCount && ei < extraCount) {
        const b = baseIds[bi], e = extraIds[ei];
        if (b < e) { i32[(pos++) << 1] = b; bi++; }
        else { i32[(pos++) << 1] = e; ei++; }
      }
      while (bi < baseCount) i32[(pos++) << 1] = baseIds[bi++];
      while (ei < extraCount) i32[(pos++) << 1] = extraIds[ei++];
    } else {
      for (; pos < basePrefixLen; pos++) out[pos] = BigInt(pos);
      let bi = basePrefixLen, ei = 0;
      while (bi < baseCount && ei < extraCount) {
        const b = baseIds[bi], e = extraIds[ei];
        if (b < e) { out[pos++] = BigInt(b); bi++; }
        else { out[pos++] = BigInt(e); ei++; }
      }
      while (bi < baseCount) out[pos++] = BigInt(baseIds[bi++]);
      while (ei < extraCount) out[pos++] = BigInt(extraIds[ei++]);
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
