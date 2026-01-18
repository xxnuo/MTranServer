import { $ } from "bun";
import fs from "fs";
import pkg from "../package.json";
import { buildMetal } from './build-metal';
import { buildCuda } from './build-cuda';
import { collectNative } from './collect-native';
import { verifyBundle } from './prepare-ort-macos-x64';
import { createRequire } from 'node:module';
import path from 'node:path';

const version = pkg.version;
const args = new Set(Bun.argv.slice(2));
const isLib = args.has("--lib");
const isNode = args.has("--node") || args.has("--dev");
const isSingle = args.has("--single");
const isAll = args.has("--all") || (!isLib && !isNode && !isSingle);
if (args.has('--publish') && !process.env.MTRAN_NATIVE_ARTIFACT_DIR)
  throw new Error('Publishing requires MTRAN_NATIVE_ARTIFACT_DIR with all six verified native artifacts');

if (isSingle || isAll) throw new Error('ONNX builds currently support --node or --lib; native runtime packaging is required for standalone releases');

console.log("Cleaning dist...");
await $`rm -rf dist`;
await $`mkdir -p dist`;
await $`bun run scripts/build-native.ts`;
if (process.env.MTRAN_NATIVE_ARTIFACT_DIR) collectNative(process.env.MTRAN_NATIVE_ARTIFACT_DIR, 'dist');
await $`bun build scripts/prepare-ort-macos-x64.ts --target=node --packages=external --outfile=dist/prepare-ort-macos-x64.js`;
await $`bun build scripts/bootstrap-runtime.ts --target=node --packages=external --outfile=dist/bootstrap-runtime.js`;
if (process.platform === 'darwin' && process.arch === 'x64' && !process.env.MTRAN_NATIVE_ARTIFACT_DIR) {
  const require = createRequire(import.meta.url);
  const ort = path.join(path.dirname(require.resolve('onnxruntime-node/package.json')), 'bin/napi-v6/darwin/x64');
  verifyBundle(ort);
  fs.cpSync(ort, 'dist/ort/darwin/x64', { recursive: true });
}
for (const dependency of ['gemmology', 'xsimd']) {
  const directory = `dist/licenses/${dependency}`;
  fs.mkdirSync(directory, { recursive: true });
  for (const filename of ['LICENSE', 'PROVENANCE.json'])
    fs.copyFileSync(`native/ssru/third_party/${dependency}/${filename}`, `${directory}/${filename}`);
}
fs.mkdirSync('dist/licenses/onnxruntime', { recursive: true });
fs.copyFileSync('native/onnx-async/LICENSE.onnxruntime', 'dist/licenses/onnxruntime/LICENSE');
buildMetal();
buildCuda();

if (isLib && fs.existsSync("ui/dist/assets")) {
  const entries = fs.readdirSync("ui/dist/assets");
  for (const entry of entries) {
    if (entry.endsWith(".d.ts")) {
      fs.rmSync(`ui/dist/assets/${entry}`);
    }
  }
}

console.log("Building UI...");
if (!fs.existsSync("ui/node_modules")) {
  await $`cd ui && bun install`;
}
await $`cd ui && bun run build`;

console.log("Generating UI assets map...");
await $`bun run scripts/gen-ui-assets.ts`;

console.log("Generating Swagger assets map...");
await $`bun run scripts/gen-swagger-assets.ts`;

console.log("Generating routes and spec...");
await $`bun tsoa spec-and-routes`;

if (isLib || isNode) {
  await $`bun build src/converter/worker.ts --outfile dist/converter-worker.js --target node --format esm --external onnxruntime-node --external @sctg/sentencepiece-js --banner "import './bootstrap-runtime.js';"`;
}

if (isLib) {
  console.log("Building library...");
  await $`bun build src/index.ts src/main.ts --outdir dist --target node --format esm --external onnxruntime-node --external @sctg/sentencepiece-js --external zstd-wasm-decoder --external express --external express-validator --external fzstd --external lru-cache --external swagger-ui-express --banner "import './bootstrap-runtime.js';"`;
  await $`tsc -p tsconfig.lib.json`;
  console.log("Build complete!");
  process.exit(0);
}

if (isNode) {
  console.log("Building for Node...");
  await $`bun build src/main.ts src/desktop.ts --outdir dist --target node --format esm --minify --external onnxruntime-node --external @sctg/sentencepiece-js --banner "import './bootstrap-runtime.js';"`;
  console.log("Build complete!");
  process.exit(0);
}
