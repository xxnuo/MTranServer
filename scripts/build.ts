import { $ } from "bun";
import fs from "fs";
import pkg from "../package.json";

const version = pkg.version;
const args = new Set(Bun.argv.slice(2));
const isDocker = args.has("--docker");
const isLib = args.has("--lib");
const isNode = args.has("--node") || args.has("--dev") || isDocker;
const isSingle = args.has("--single");
const isAll = args.has("--all") || (!isLib && !isNode && !isSingle);

const allTargets = [
  { bun: "bun-darwin-x64", name: "darwin-amd64" },
  { bun: "bun-darwin-x64-baseline", name: "darwin-amd64-legacy" },
  { bun: "bun-darwin-arm64", name: "darwin-arm64" },
  { bun: "bun-linux-x64", name: "linux-amd64" },
  { bun: "bun-linux-x64-baseline", name: "linux-amd64-legacy" },
  { bun: "bun-linux-arm64", name: "linux-arm64" },
  { bun: "bun-linux-x64-musl", name: "linux-amd64-musl" },
  { bun: "bun-linux-x64-musl-baseline", name: "linux-amd64-musl-legacy" },
  { bun: "bun-linux-arm64-musl", name: "linux-arm64-musl" },
  { bun: "bun-windows-x64", name: "windows-amd64" },
  { bun: "bun-windows-x64-baseline", name: "windows-amd64-legacy" }
];

console.log("Cleaning dist...");
await $`rm -rf dist`;
await $`mkdir -p dist`;

if (isLib) {
  await $`rm -f ui/dist/assets/*.d.ts`;
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
await $`bun run gen`;

if (isLib) {
  console.log("Building library...");
  await $`bun build src/index.ts src/main.ts --outdir dist --target node --format esm --sourcemap --external zstd-wasm-decoder --external express`;
  await $`tsc -p tsconfig.lib.json`;
  console.log("Build complete!");
  process.exit(0);
}

if (isNode) {
  console.log("Building for Node...");
  await $`bun build src/main.ts --outdir dist --target node --format esm --sourcemap --external zstd-wasm-decoder --external express`;
  console.log("Build complete!");
  process.exit(0);
}

if (isSingle) {
  console.log("Building single binary...");
  await $`bun build src/main.ts --compile --outfile ./dist/mtranserver --minify --sourcemap`;
  console.log("Build complete!");
  process.exit(0);
}

if (isAll) {
  for (const target of allTargets) {
    const ext = target.bun.includes("windows") ? ".exe" : "";
    const outfile = `dist/mtranserver-${version}-${target.name}${ext}`;
    console.log(`Building for ${target.bun} -> ${outfile}...`);
    await $`bun build src/main.ts --compile --target=${target.bun} --outfile=${outfile} --minify --sourcemap`;
  }
  console.log("Build complete!");
}
