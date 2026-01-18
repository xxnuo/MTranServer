import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdir, writeFile, appendFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import path from 'node:path';

const version = execFileSync('node', ['-p', 'process.versions.node'], { encoding: 'utf8' }).trim();
const arch = execFileSync('node', ['-p', 'process.arch'], { encoding: 'utf8' }).trim();
assert(/^\d+\.\d+\.\d+$/.test(version));
assert(['x64', 'arm64'].includes(arch));
const base = `https://nodejs.org/dist/v${version}/`;
const response = await fetch(base + 'SHASUMS256.txt');
assert(response.ok, 'Cannot load official Node checksums');
const checksums = await response.text();
const directory = path.resolve('.tmp', `node-${version}-${arch}`);
await mkdir(directory, { recursive: true });
async function download(name: string, destination: string) {
  const expected = checksums.split('\n').map(line => line.trim().split(/\s+/)).find(row => row[1] === name)?.[0];
  assert(expected, `Missing official checksum for ${name}`);
  const response = await fetch(base + name);
  assert(response.ok, `Cannot download ${name}`);
  const bytes = new Uint8Array(await response.arrayBuffer());
  assert.equal(createHash('sha256').update(bytes).digest('hex'), expected, `Checksum mismatch: ${name}`);
  await writeFile(destination, bytes);
}
const archive = `node-v${version}-headers.tar.gz`;
await download(archive, path.join(directory, archive));
execFileSync('tar', ['-xzf', archive], { cwd: directory });
let environment = `NODE_INCLUDE_DIR=${path.join(directory, `node-v${version}`, 'include/node')}\n`;
if (process.platform === 'win32') {
  const library = path.join(directory, 'node.lib');
  await download(`win-${arch}/node.lib`, library);
  environment += `NODE_LIB_FILE=${library}\n`;
}
if (process.env.GITHUB_ENV) await appendFile(process.env.GITHUB_ENV, environment);
console.log(environment.trim());
