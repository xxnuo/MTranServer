import assert from 'node:assert/strict';
import { test } from 'bun:test';
import protobuf from 'protobufjs';
import * as ort from 'onnxruntime-node';
import { parseLexicon, generateCandidates, validateRequest, checksum, byteTokenIds } from '../scripts/onnx-shortlist/shortlist.ts';
import { modelType, rewriteProjection, value, node, ia, isa } from '../scripts/onnx-shortlist/variant.ts';

const binary = (first: number, offsets: number[], targets: number[]) => {
  const bytes = Buffer.alloc(48 + offsets.length * 8 + targets.length * 4);
  [0xF11A48D5013417F5n, 0n, BigInt(first), 50n, BigInt(offsets.length), BigInt(targets.length)]
    .forEach((x, i) => bytes.writeBigUInt64LE(x, i * 8));
  offsets.forEach((x, i) => bytes.writeBigUInt64LE(BigInt(x), 48 + i * 8));
  targets.forEach((x, i) => bytes.writeUInt32LE(x, 48 + offsets.length * 8 + i * 4));
  bytes.writeBigUInt64LE(checksum(bytes.subarray(16)), 8);
  return bytes;
};
test('lexicon validates size, checksum, offsets and IDs; candidates are sorted and padded', () => {
const lex = parseLexicon(binary(1, Array(17).fill(0), []), 16, 16);
assert.deepEqual(generateCandidates(lex, [9, 9], 16, true, 0), [0, 1, 2, 3, 4, 5, 6, 9]);
assert.deepEqual(generateCandidates(lex, [9, 9], 16, false, 0), [0, 1, 2, 3, 4, 5, 6, 7]);
const odd = binary(1, [0, 1], [7]);
assert.deepEqual(parseLexicon(odd, 8).targets, [7]);
assert.throws(() => parseLexicon(odd.subarray(0, -1), 8));
const corrupt = Buffer.from(odd); corrupt[16] ^= 1;
assert.throws(() => parseLexicon(corrupt, 8), /checksum/);
assert.throws(() => parseLexicon(binary(1, [0, 2, 1], [4]), 8), /offset/);
assert.throws(() => parseLexicon(binary(1, [0, 1], [8]), 8), /target/);
assert.throws(() => generateCandidates(lex, [16], 16, true, 0), /source/);
for (const ids of [[], [-1, 0], [0, 0], [1, 0], [0, 16], [0, 1.5], [1]]) {
  assert.throws(() => validateRequest([0], ids, 16, 16, 0));
}
assert.throws(() => validateRequest([], [0, 1], 16, 16, 0));
assert.throws(() => validateRequest([16], [0, 1], 16, 16, 0));
validateRequest([0], [0, 1], 16, 16, 0);
assert.throws(() => parseLexicon(binary(17, [0, 0], []), 16), /prefix/);
assert.throws(() => parseLexicon(binary(1, [0, 0], []), 16, 2), /source/);
const oversized = binary(1, [0, 0], []); oversized.writeBigUInt64LE(2n ** 63n, 32);
assert.throws(() => parseLexicon(oversized, 16), /oversized/);
assert.deepEqual(generateCandidates(lex, [9], 16, true, 0, [14, 15]), [0, 1, 2, 3, 4, 9, 14, 15]);
assert.throws(() => generateCandidates(lex, [0], 16, false, 0, [16]), /byte/);
assert.throws(() => generateCandidates(lex, [0], 16, false, 0, [1.5]), /byte/);
assert.deepEqual(generateCandidates(lex, [0], 16, false, 0, Array.from({ length: 16 }, (_, i) => i)), Array.from({ length: 16 }, (_, i) => i));
});

test('SentencePiece byte IDs follow type 6 at arbitrary positions, with bounded messages', () => {
  const piece = (type?: number) => { const w = protobuf.Writer.create(); w.uint32(10).string('piece'); if (type !== undefined) w.uint32(24).int32(type); return w.finish(); };
  const w = protobuf.Writer.create();
  for (const type of [6, 1, undefined, 6, 3, 6]) w.uint32(10).bytes(piece(type));
  w.uint32(18).bytes(Uint8Array.of(8, 1));
  assert.deepEqual(byteTokenIds(w.finish(), 6), [0, 3, 5]);
  assert.throws(() => byteTokenIds(w.finish(), 5), /vocab/);
  for (const bytes of [[10, 5, 24, 6], [10, 1, 24], [0], [10, 2, 10, 5]]) assert.throws(() => byteTokenIds(Uint8Array.from(bytes)));
});

test('tiny converted graph maps first and Loop candidate indices, masks token 1 and stops at EOS', async () => {

// Tiny graph: first hidden selects token 4; loop hidden selects EOS 2.
const projection = (prefix: string, hidden: string) => [
  node('MatMulNBits', [hidden, 'projection_Q4', 'projection_scales'], prefix + 'q'),
  node('Add', [prefix + 'q', 'decoder_ff_logit_out_b'], prefix + 'bias'),
  { ...node('TopK', [prefix + 'bias', 'k'], prefix + 'scores'), output: [prefix + 'scores', prefix + 'ids'] },
  node('Squeeze', [prefix + 'ids'], prefix + 'flat', isa('axes', 0)),
  node('Gather', ['Wemb', prefix + 'flat'], prefix + 'w', ia('axis', 0)),
  node('Gather', ['decoder_ff_logit_out_b', prefix + 'flat'], prefix + 'b', ia('axis', 1)),
  node('Transpose', [prefix + 'w'], prefix + 't', isa('perm', 1, 0)),
  node('MatMul', [hidden, prefix + 't'], prefix + 'mm'),
  node('Add', [prefix + 'mm', prefix + 'b'], prefix + 'logits'),
  node('ArgMax', [prefix + 'logits'], prefix + 'arg', ia('axis', 1), ia('keepdims', 0)),
  node('Squeeze', [prefix + 'arg'], prefix + 'index', isa('axes', 0)),
  node('Gather', [prefix + 'flat', prefix + 'index'], prefix + 'token', ia('axis', 0)),
];
const floatTensor = (name: string, dims: number[], values: number[]) => ({ name, dims, dataType: 1, rawData: Buffer.from(new Float32Array(values).buffer) });
const body = {
  name: 'decode_loop', initializer: [],
  input: [value('iter', 7), value('cond_in', 9), value('prev', 7)],
  output: [value('cond_out', 9), value('next_token', 7), value('scan', 7)],
  node: [...projection('loop_', 'loop_hidden'),
    node('Equal', ['loop_token', 'eos'], 'is_eos'), node('Not', ['is_eos'], 'cond_out'),
    node('Identity', ['loop_token'], 'next_token'), node('Identity', ['loop_token'], 'scan')],
};
const graph = {
  name: 'synthetic', input: [value('X', 7, -1), value('eos_id', 7, 1)], output: [value('Y', 7, -1)],
  initializer: [floatTensor('Wemb', [10, 2], [0, 0, 100, 100, 3, 0, 0, 0, 0, 3, ...Array(10).fill(0)]),
    floatTensor('decoder_ff_logit_out_b', [1, 10], [0, -Infinity, ...Array(8).fill(0)]),
    floatTensor('first_hidden', [1, 2], [0, 1]), floatTensor('loop_hidden', [1, 2], [1, 0]),
    { name: 'eos', dataType: 7, dims: [], int64Data: [2] },
    { name: 'trip', dataType: 7, dims: [], int64Data: [3] }],
  node: [...projection('first_', 'first_hidden'), node('Equal', ['first_token', 'eos'], 'first_eos'),
    node('Not', ['first_eos'], 'continue'),
    { ...node('Loop', ['trip', 'continue', 'first_token'], 'last'), output: ['last', 'generated'], attribute: [{ name: 'body', type: 5, g: body }] },
    node('Unsqueeze', ['first_token'], 'first_vec', isa('axes', 0)),
    node('Concat', ['first_vec', 'generated'], 'Y', ia('axis', 0))],
};
const preserved = graph.initializer.map(t => ({ ...t, rawData: t.rawData ? Buffer.from(t.rawData) : undefined }));
graph.initializer.push(
  { name: 'projection_Q4', dataType: 2, dims: [10, 1, 32], rawData: Buffer.alloc(320) } as any,
  floatTensor('projection_scales', [10, 1], Array(10).fill(1)),
  { name: 'k', dataType: 7, dims: [1], int64Data: [9] });
const model = { irVersion: 10, graph, opsetImport: [{ domain: '', version: 11 }] };
// protobufjs omits empty repeated fields on decoded real Loop subgraphs.
delete (body as any).initializer;
const report = rewriteProjection(model);
assert.equal(report.replaced, 2);
assert.deepEqual(new Set(report.removedInitializers), new Set(['projection_Q4', 'projection_scales']));
for (const original of preserved) {
  const current = graph.initializer.find(t => t.name === original.name)!;
  assert.deepEqual({ ...current, rawData: current.rawData ? Buffer.from(current.rawData) : undefined }, original);
}
assert.equal(body.node.filter(n => n.opType === 'Transpose').length, 0);
const session = await ort.InferenceSession.create(modelType.encode(modelType.create(model)).finish(), { intraOpNumThreads: 1, interOpNumThreads: 1 });
try {
  for (const [ids, expected] of [[[1, 2, 4], [4, 2]], [[1, 2], [2]]] as const) {
    const feeds = {
      X: new ort.Tensor('int64', BigInt64Array.of(0n), [1]),
      eos_id: new ort.Tensor('int64', BigInt64Array.of(2n), [1]),
      candidate_ids: new ort.Tensor('int64', BigInt64Array.from(ids, BigInt), [ids.length]),
    };
    const outputs = await session.run(feeds);
    assert.deepEqual(Array.from(outputs.Y.data as BigInt64Array, Number), expected);
    Object.values(outputs).forEach(t => t.dispose()); Object.values(feeds).forEach(t => t.dispose());
  }
} finally { await session.release(); }
});

test('artifact reservation rejects existing and escaping directories', async () => {
  const { mkdir, mkdtemp, rm, symlink } = await import('node:fs/promises');
  const path = await import('node:path');
  const { reserveOutput } = await import('../scripts/onnx-shortlist/index.ts');
  const tmp = path.resolve(import.meta.dir, '../.tmp');
  await mkdir(tmp, { recursive: true });
  const root = await mkdtemp(path.join(tmp, 'shortlist-path-test-'));
  try {
    const output = path.join(root, 'new');
    assert.equal(await reserveOutput(output), output);
    await assert.rejects(reserveOutput(output), /EEXIST/);
    await assert.rejects(reserveOutput(path.resolve(tmp, '../models/new')), /inside project/);
    await symlink(path.resolve(tmp, '..'), path.join(root, 'link'), 'dir');
    await assert.rejects(reserveOutput(path.join(root, 'link/new')), /symlinks/);
  } finally { await rm(root, { recursive: true, force: true }); }
});
