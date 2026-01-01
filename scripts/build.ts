import { $ } from "bun";

const targets = [
  "bun-linux-x64",
  "bun-linux-arm64",
  "bun-windows-x64",
  "bun-darwin-x64",
  "bun-darwin-arm64",
];

console.log("Cleaning dist...");
await $`rm -rf dist`;
await $`mkdir -p dist`;

for (const target of targets) {
  const ext = target.includes("windows") ? ".exe" : "";
  const outfile = `dist/mtranserver-${target}${ext}`;
  console.log(`Building for ${target}...`);
  await $`bun build src/main.ts --compile --target=${target} --outfile=${outfile} --minify --sourcemap`;
}

console.log("Build complete!");
