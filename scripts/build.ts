import { $ } from "bun";
import pkg from "../package.json";

const version = pkg.version;

const targets = [
  { bun: "bun-darwin-x64", name: "darwin-amd64" },
  { bun: "bun-darwin-x64-baseline", name: "darwin-amd64-legacy" },
  { bun: "bun-darwin-arm64", name: "darwin-arm64" },
  { bun: "bun-linux-x64", name: "linux-amd64" },
  { bun: "bun-linux-x64-baseline", name: "linux-amd64-legacy" },
  { bun: "bun-linux-arm64", name: "linux-arm64" },
  { bun: "bun-linux-x64-musl", name: "linux-amd64-musl" },
  { bun: "bun-linux-arm64-musl", name: "linux-arm64-musl" },
  { bun: "bun-windows-x64", name: "windows-amd64" },
  { bun: "bun-windows-x64-baseline", name: "windows-amd64-legacy" }
];

console.log("Cleaning dist...");
await $`rm -rf dist`;
await $`mkdir -p dist`;

for (const target of targets) {
  const ext = target.bun.includes("windows") ? ".exe" : "";
  const outfile = `dist/mtranserver-${version}-${target.name}${ext}`;
  console.log(`Building for ${target.bun} -> ${outfile}...`);
  await $`bun build src/main.ts --compile --target=${target.bun} --outfile=${outfile} --minify --sourcemap`;
}

console.log("Build complete!");
