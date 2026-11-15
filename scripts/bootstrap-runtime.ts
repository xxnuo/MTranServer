import { existsSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { files, prepareOrtMacosX64 } from './prepare-ort-macos-x64';

if (process.platform === 'darwin' && process.arch === 'x64') {
  const require = createRequire(import.meta.url);
  const destination = path.join(path.dirname(require.resolve('onnxruntime-node/package.json')), 'bin/napi-v6/darwin/x64');
  if (files.some(filename => !existsSync(path.join(destination, filename)))) {
    // Synchronous bootstrap runs before the external ORT module is evaluated.
    prepareOrtMacosX64(fileURLToPath(new URL('..', import.meta.url)), true);
  }
}
