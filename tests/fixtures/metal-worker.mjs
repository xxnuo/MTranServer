const mode = process.argv[2];
const integer = n => { const out = Buffer.alloc(4); out.writeUInt32LE(n); return out; };
if (mode !== 'no-ready') process.stdout.write(integer(0));
let input = Buffer.alloc(0);
process.stdin.on('data', data => {
  input = Buffer.concat([input, data]);
  if (input.length < 8) return;
  const count = input.readUInt32LE(0), rows = [];
  let offset = 8;
  for (let i = 0; i < count; i++) {
    if (input.length < offset + 4) return;
    const n = input.readUInt32LE(offset); offset += 4;
    if (input.length < offset + n * 4 + 4) return;
    rows.push(Array.from({ length: n }, (_, j) => input.readUInt32LE(offset + j * 4)));
    offset += n * 4;
    const k = input.readUInt32LE(offset); offset += 4;
    if (input.length < offset + k * 4) return;
    offset += k * 4;
  }
  input = input.subarray(offset);
  if (mode === 'crash') process.exit(7);
  if (mode === 'truncated') { process.stdout.write(Buffer.from([0, 0])); process.exit(1); }
  if (mode === 'wrong-count') { process.stdout.write(Buffer.concat([integer(0), integer(count + 1)])); return; }
  const parts = [integer(0), integer(count)];
  for (const ids of rows) {
    const tokens = mode === 'invalid-token' ? [0xffff_ffff] : ids.slice(0, -1);
    parts.push(integer(tokens.length), ...tokens.map(integer));
  }
  const response = Buffer.concat(parts);
  let next = 0;
  const write = () => { if (next < response.length) { process.stdout.write(response.subarray(next, ++next)); setImmediate(write); } };
  write();
});
