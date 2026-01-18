import { readFile } from 'node:fs/promises';
import { parse } from 'yaml';

export interface Weight { dims: number[]; values: Float32Array }
export type ModelConfig = Record<string, any>;

export async function readRelease(file: string) {
  const data = await readFile(file);
  let offset = 0;
  const take = (n: number) => {
    if (!Number.isSafeInteger(n) || n < 0 || n > data.length - offset) throw new Error(`Truncated model at ${offset}`);
    const bytes = data.subarray(offset, offset += n);
    return bytes;
  };
  const uint = (bytes: Buffer, offset = 0) => {
    const n = bytes.readBigUInt64LE(offset);
    if (n > BigInt(Number.MAX_SAFE_INTEGER)) throw new Error('Oversized model field');
    return Number(n);
  };
  const header = take(16), count = uint(header, 8);
  if (uint(header) !== 1 || count > (data.length - 16) / 32) throw new Error('Invalid model header');
  const headers = Array.from({ length: count }, () => {
    const h = take(32);
    return { nameLength: uint(h), kind: uint(h, 8), rank: uint(h, 16), size: uint(h, 24), name: '', dims: [] as number[] };
  });
  const names = new Set<string>();
  for (const h of headers) {
    const name = take(h.nameLength);
    if (name.length < 2 || name.at(-1) !== 0) throw new Error('Invalid tensor name');
    h.name = name.subarray(0, -1).toString();
    if (names.has(h.name)) throw new Error('Duplicate tensor name');
    names.add(h.name);
  }
  for (const h of headers) {
    if (h.rank < 1 || h.rank > 8) throw new Error('Unsupported tensor rank');
    const dims = take(h.rank * 4);
    h.dims = Array.from({ length: h.rank }, (_, i) => dims.readInt32LE(i * 4));
    if (h.dims.some(d => d <= 0)) throw new Error('Invalid tensor dimensions');
  }
  take(uint(take(8)));
  const weights = new Map<string, Weight>();
  let config: ModelConfig | undefined;
  for (const h of headers) {
    const bytes = take(h.size);
    const n = h.dims.reduce((n, d) => n * d, 1);
    if (!Number.isSafeInteger(n) || n > data.length) throw new Error('Oversized tensor');
    if (h.name === 'special:model.yml' && h.kind === 0x101) {
      if (n > bytes.length) throw new Error('Truncated metadata');
      config = parse(bytes.subarray(0, n).toString().replace(/\0+$/, ''));
      continue;
    }
    let values: Float32Array;
    if (h.kind === 0x404) {
      if (n * 4 > bytes.length) throw new Error('Truncated float tensor');
      values = new Float32Array(n);
      for (let i = 0; i < n; i++) values[i] = bytes.readFloatLE(i * 4);
    } else if (h.kind === 0x4101 || h.kind === 0x4102) {
      const width = h.kind === 0x4101 ? 1 : 2;
      if (n * width + 4 > bytes.length) throw new Error('Missing quantization multiplier');
      const multiplier = bytes.readFloatLE(n * width);
      if (!(multiplier > 0) || !Number.isFinite(multiplier)) throw new Error('Invalid quantization multiplier');
      const inverse = Math.fround(1 / multiplier);
      values = new Float32Array(n);
      for (let i = 0; i < n; i++) values[i] = (width === 1 ? bytes.readInt8(i) : bytes.readInt16LE(i * 2)) * inverse;
      if (!h.name.includes('Wemb')) {
        if (h.dims.length !== 2) throw new Error('Non-matrix intgemm tensor');
        const [rows, cols] = h.dims, transposed = new Float32Array(n);
        for (let i = 0; i < rows; i++) for (let j = 0; j < cols; j++) transposed[i * cols + j] = values[j * rows + i];
        values = transposed;
      }
    } else throw new Error(`Unsupported tensor type ${h.kind.toString(16)}`);
    weights.set(h.name, { dims: h.dims, values });
  }
  if (!config || offset !== data.length) throw new Error('Missing metadata or trailing bytes');
  const c = config;
  if ((c['transformer-postprocess-emb'] || 'd') !== 'd' || c['transformer-train-position-embeddings'] || c['transformer-rnn-projection'] || c['transformer-no-projection'] || c['right-left'] || c['transformer-tied-layers']?.length || (c['transformer-decoder-ffn-depth'] && c['transformer-decoder-ffn-depth'] !== c['transformer-ffn-depth'])) throw new Error('Unsupported architecture options');
  if (c.type !== 'transformer' || c['dec-cell'] !== 'ssru' || c['transformer-decoder-autoreg'] !== 'rnn' || c['transformer-ffn-activation'] !== 'relu' || c['transformer-preprocess'] || c['transformer-postprocess'] !== 'dan' || c['transformer-postprocess-top'] || !(c['tied-embeddings'] || c['tied-embeddings-all'])) throw new Error('Unsupported model architecture');
  const dim = c['dim-emb'], heads = c['transformer-heads'];
  if (!Number.isInteger(dim) || dim < 4 || dim % 2 || !Number.isInteger(heads) || heads < 1 || dim % heads || ['enc-depth', 'dec-depth', 'transformer-ffn-depth'].some(k => !Number.isInteger(c[k]) || c[k] < 1)) throw new Error('Invalid model dimensions');
  return { weights, config };
}
