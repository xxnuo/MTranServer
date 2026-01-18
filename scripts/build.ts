import { $ } from "bun";
import fs from "fs";
import pkg from "../package.json";

const version = pkg.version;
const args = new Set(Bun.argv.slice(2));
const isLib = args.has("--lib");
const isNode = args.has("--node") || args.has("--dev");
const isSingle = args.has("--single");
const isAll = args.has("--all") || (!isLib && !isNode && !isSingle);

if (isSingle || isAll) throw new Error('ONNX builds currently support --node or --lib; native runtime packaging is required for standalone releases');

console.log("Cleaning dist...");
await $`rm -rf dist`;
await $`mkdir -p dist`;

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
  await $`bun build src/converter/worker.ts --outfile dist/converter-worker.js --target node --format esm --external onnxruntime-node --external @sctg/sentencepiece-js`;
}

if (isLib) {
  console.log("Building library...");
  await $`bun build src/index.ts src/main.ts --outdir dist --target node --format esm --external onnxruntime-node --external @sctg/sentencepiece-js --external zstd-wasm-decoder --external express --external express-validator --external fzstd --external lru-cache --external swagger-ui-express`;
  await $`tsc -p tsconfig.lib.json`;
  console.log("Build complete!");
  process.exit(0);
}

if (isNode) {
  console.log("Building for Node...");
  await $`bun build src/main.ts src/desktop.ts --outdir dist --target node --format esm --minify --external onnxruntime-node --external @sctg/sentencepiece-js`;
  console.log("Build complete!");
  process.exit(0);
}
