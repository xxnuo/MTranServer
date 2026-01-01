import { $ } from "bun";
import fs from "fs";
import path from "path";

const distDir = "ui/dist";
const outputFile = "src/assets/ui.ts";

function* walkDir(dir: string): Generator<string> {
  const entries = fs.readdirSync(dir, { withFileTypes: true });
  for (const entry of entries) {
    const fullPath = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      yield* walkDir(fullPath);
    } else {
      yield fullPath;
    }
  }
}

const files = Array.from(walkDir(distDir));

const imports = files
  .map((file, i) => {
    const absolutePath = path.resolve(file);
    return `import _asset${i} from "${absolutePath}" with { type: "file" };`;
  })
  .join("\n");

const assetMap = files
  .map((file, i) => {
    const urlPath = "/" + path.relative(distDir, file).replace(/\\/g, "/");
    return `  "${urlPath}": _asset${i}`;
  })
  .join(",\n");

const code = `${imports}

export const assets: Record<string, string> = {
${assetMap}
};
`;

await Bun.write(outputFile, code);
console.log(`Generated ${outputFile} with ${files.length} assets`);
