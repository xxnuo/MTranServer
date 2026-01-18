import { existsSync } from 'node:fs';
import { createRequire } from 'node:module';
import { availableParallelism } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { isMainThread } from 'node:worker_threads';
import { files, prepareOrtMacosX64 } from './prepare-ort-macos-x64';

if (isMainThread && !process.env.UV_THREADPOOL_SIZE) {
  process.env.UV_THREADPOOL_SIZE = String(Math.min(16, Math.max(4, availableParallelism())));
}

if (process.platform === 'darwin' && process.arch === 'x64') {
  const require = createRequire(import.meta.url);
  const destination = path.join(path.dirname(require.resolve('onnxruntime-node/package.json')), 'bin/napi-v6/darwin/x64');
  if (files.some(filename => !existsSync(path.join(destination, filename)))) {
    // Synchronous bootstrap runs before the external ORT module is evaluated.
    prepareOrtMacosX64(fileURLToPath(new URL('..', import.meta.url)), true);
  }
}
