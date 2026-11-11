import assert from 'node:assert/strict';
import protobuf from 'protobufjs';
import schema from '../../src/converter/schema.json';

// shortcut: surgery recognizes only graph.ts's current K9 pattern; reject changed graphs and revisit before production use.
export const modelType = protobuf.Root.fromJSON(schema).lookupType('onnx.ModelProto');
export const ia = (name: string, i: number) => ({ name, type: 2, i });
export const isa = (name: string, ...ints: number[]) => ({ name, type: 7, ints });
export const value = (name: string, elemType: number, ...dims: number[]) => ({ name, type: { tensorType: { elemType, shape: { dim: dims.map(d => d < 0 ? { dimParam: name + '_n' } : { dimValue: d }) } } } });
export const node = (opType: string, input: string[], output: string, ...attribute: object[]) => ({ name: output + '_node', opType, input, output: [output], attribute });

function graphs(g: any): any[] {
  return [g, ...g.node.flatMap((n: any) => (n.attribute ?? []).flatMap((a: any) => [...(a.g ? graphs(a.g) : []), ...(a.graphs ?? []).flatMap(graphs)]))];
}
export function rewriteProjection(model: any) {
  const root = model.graph, scopes = graphs(root);
  assert.equal(scopes.length, 2, 'expected root and one Loop body');
  assert.equal(root.node.filter((n: any) => n.opType === 'Loop').length, 1);
  assert(!scopes.some(g => [...g.input, ...g.output, ...(g.initializer ?? []), ...g.node.flatMap((n: any) => n.output)].some((v: any) => {
    const name = typeof v === 'string' ? v : v.name;
    return name.startsWith('shortlist_') || name === 'candidate_ids';
  })), 'shortlist names already exist');
  const embedding = root.initializer.find((t: any) => t.name === 'Wemb') ?? root.initializer.find((t: any) => t.name === 'decoder_Wemb');
  assert(embedding && embedding.dataType === 1 && embedding.dims.length === 2, 'missing FP32 target embedding');
  const [vocab, dim] = embedding.dims;
  assert(Number.isSafeInteger(vocab) && vocab >= 9 && Number.isSafeInteger(dim) && dim > 0);
  assert.equal(embedding.rawData?.length, vocab * dim * 4, 'invalid embedding bytes');
  const bias = root.initializer.find((t: any) => t.name === 'decoder_ff_logit_out_b');
  assert(bias && bias.dataType === 1 && JSON.stringify(bias.dims) === JSON.stringify([1, vocab]), 'invalid output bias');
  assert.equal(bias.rawData?.length, vocab * 4, 'invalid bias bytes');
  assert.equal(Buffer.from(bias.rawData).readFloatLE(4), -Infinity, 'token 1 must already be masked');
  let replaced = 0;
  for (const g of scopes) {
    const matches = g.node.flatMap((n: any, i: number) => n.opType === 'MatMulNBits' ? [i] : []);
    assert.equal(matches.length, 1, 'expected exactly one full-vocab projection per scope');
    const start = matches[0], old = g.node.slice(start, start + 12);
    assert.deepEqual(old.map((n: any) => n.opType), ['MatMulNBits', 'Add', 'TopK', 'Squeeze', 'Gather', 'Gather', 'Transpose', 'MatMul', 'Add', 'ArgMax', 'Squeeze', 'Gather']);
    const out = (i: number, j = 0) => old[i].output[j];
    assert.deepEqual(old.map((n: any) => n.input), [
      [old[0].input[0], 'projection_Q4', 'projection_scales'],
      [out(0), bias.name], [out(1), old[2].input[1]], [out(2, 1)],
      [embedding.name, out(3)], [bias.name, out(3)], [out(4)],
      [old[0].input[0], out(6)], [out(7), out(5)], [out(8)], [out(9)], [out(3), out(10)],
    ], 'projection wiring changed');
    const attribute = (i: number, name: string) => old[i].attribute?.find((a: any) => a.name === name);
    assert.equal(attribute(0, 'K')?.i ?? dim, dim);
    assert.equal(attribute(0, 'N')?.i ?? vocab, vocab);
    for (const i of [4, 11]) assert.equal(attribute(i, 'axis')?.i, 0);
    assert.equal(attribute(5, 'axis')?.i, 1);
    assert.deepEqual(attribute(6, 'perm')?.ints, [1, 0]);
    assert.equal(attribute(9, 'axis')?.i, 1);
    assert.equal(attribute(9, 'keepdims')?.i, 0);
    for (const i of [3, 10]) assert.deepEqual(attribute(i, 'axes')?.ints, [0]);
    const removed = new Set(old.slice(0, -1).flatMap((n: any) => n.output));
    assert(!g.node.filter((_: any, i: number) => i < start || i >= start + 12).some((n: any) => n.input.some((s: string) => removed.has(s))), 'projection intermediates have other consumers');
    assert(!g.output.some((v: any) => removed.has(v.name)), 'projection intermediates are graph outputs');
    if (g.valueInfo) g.valueInfo = g.valueInfo.filter((v: any) => !removed.has(v.name));
    const p = `shortlist_${replaced++}_`;
    g.node.splice(start, 12,
      node('MatMul', [old[0].input[0], 'shortlist_projection_T'], p + 'matmul'),
      node('Add', [p + 'matmul', 'shortlist_bias'], p + 'logits'),
      node('ArgMax', [p + 'logits'], p + 'argmax', ia('axis', 1), ia('keepdims', 0)),
      node('Squeeze', [p + 'argmax'], p + 'index', isa('axes', 0)),
      node('Gather', ['candidate_ids', p + 'index'], out(11), ia('axis', 0)));
  }
  root.input.push(value('candidate_ids', 7, -1));
  // Both first-token projection and Loop capture this single per-request candidate matrix.
  root.node.unshift(
    node('Gather', [embedding.name, 'candidate_ids'], 'shortlist_embedding', ia('axis', 0)),
    node('Gather', [bias.name, 'candidate_ids'], 'shortlist_bias', ia('axis', 1)),
    node('Transpose', ['shortlist_embedding'], 'shortlist_projection_T', isa('perm', 1, 0)));
  const used = new Set(scopes.flatMap(g => g.node.flatMap((n: any) => n.input)));
  const removedInitializers = root.initializer.filter((t: any) => ['projection_Q4', 'projection_scales'].includes(t.name) && !used.has(t.name)).map((t: any) => t.name);
  root.initializer = root.initializer.filter((t: any) => !removedInitializers.includes(t.name));
  assert(!scopes.some(g => g.node.some((n: any) => ['MatMulNBits', 'TopK'].includes(n.opType))));
  assert(!root.initializer.some((t: any) => ['projection_Q4', 'projection_scales'].includes(t.name)));
  assert.equal(modelType.verify(model), null);
  return { replaced, vocab, dim, embedding: embedding.name, removedInitializers, projection: 'FP32 candidate MatMul', intermediateLayers: 'original intermediate layers unchanged', weightPacking: 'dynamic Gather; no session-time candidate-weight prepacking' };
}
