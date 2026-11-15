import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

if (process.platform === 'darwin' && process.arch === 'x64') {
  const script = new URL('../dist/prepare-ort-macos-x64.js', import.meta.url);
  if (existsSync(script)) {
    const { prepareOrtMacosX64 } = await import(script.href);
    prepareOrtMacosX64(fileURLToPath(new URL('..', import.meta.url)), true);
  } else if (!existsSync(new URL('../src/main.ts', import.meta.url))) {
    throw new Error('Missing packaged Intel Mac ONNX Runtime installer');
  }
}
