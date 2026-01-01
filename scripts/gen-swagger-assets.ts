import fs from "fs";
import path from "path";

const distDir = "node_modules/swagger-ui-dist";
const outputFile = "src/assets/swagger.ts";

const neededExtensions = [".css", ".js", ".png"];
const excludePatterns = [/\.map$/, /^index\./, /absolute-path/, /-es-bundle/];

const files = fs
  .readdirSync(distDir)
  .filter((f) => {
    const ext = path.extname(f);
    if (!neededExtensions.includes(ext)) return false;
    if (excludePatterns.some((p) => p.test(f))) return false;
    return true;
  })
  .map((f) => path.join(distDir, f));

const imports = files
  .map((file, i) => {
    const absolutePath = path.resolve(file);
    return `import _asset${i} from "${absolutePath}" with { type: "file" };`;
  })
  .join("\n");

const assetMap = files
  .map((file, i) => {
    const urlPath = "/" + path.basename(file);
    return `  "${urlPath}": _asset${i}`;
  })
  .join(",\n");

const code = `${imports}

export const swaggerAssets: Record<string, string> = {
${assetMap}
};
`;

await Bun.write(outputFile, code);
console.log(`Generated ${outputFile} with ${files.length} assets`);
