import { createHash } from 'node:crypto';
import { readFileSync, readdirSync } from 'node:fs';
import path from 'node:path';

export function nativeInputHashes(root = process.cwd()): Record<string, string> {
  const files = ['scripts/build-native.ts', 'scripts/build-portable-native.sh', 'native/ssru/Dockerfile.portable',
    'scripts/native-inputs.ts', 'scripts/verify-ssru-platform.ts',
    'scripts/ssru-platform-models.json', 'src/converter/graph.ts', 'src/converter/release.ts'];
  const sources = new Set([
    'addon.cc', 'ssru_op.cc', 'ssru.cc', 'encoder.cc', 'ops.cc', 'pcore_pool.cc',
    'packed.cc', 'packed_vnni.cc', 'packed_avx512vnni.cc',
  ]);
  function visit(directory: string) {
    for (const entry of readdirSync(path.join(root, directory), { withFileTypes: true })) {
      const name = `${directory}/${entry.name}`;
      if (entry.isDirectory() && !entry.name.startsWith('.')) visit(name);
      else if (entry.isFile() && (/\.(h|hpp)$/.test(entry.name) || sources.has(entry.name))) files.push(name);
    }
  }
  visit('native/onnx-async'); visit('native/ssru');
  return Object.fromEntries(files.sort().map(filename => [filename, createHash('sha256').update(readFileSync(path.join(root, filename))).digest('hex')]));
}
