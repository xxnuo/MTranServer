import { file, write } from "bun";

const newVersionArg = Bun.argv[2];

if (!newVersionArg) {
  console.error("Usage: bun run bump <version>");
  process.exit(1);
}

// Strip 'v' prefix if present
const newVersion = newVersionArg.startsWith("v") ? newVersionArg.slice(1) : newVersionArg;

// Validate SemVer (simple regex)
const versionRegex = /^\d+\.\d+\.\d+$/;
if (!versionRegex.test(newVersion)) {
  console.error(`Invalid version format: ${newVersion}. Expected x.y.z`);
  process.exit(1);
}

console.log(`Bumping version to ${newVersion}...`);

async function updateJsonFile(filePath: string, versionPath: string[]) {
  try {
    const f = file(filePath);
    if (!(await f.exists())) {
        console.warn(`File not found: ${filePath}`);
        return;
    }
    const content = await f.json();
    
    let current = content;
    for (let i = 0; i < versionPath.length - 1; i++) {
      if (current[versionPath[i]] === undefined) {
         console.error(`Path not found in ${filePath}: ${versionPath.join('.')}`);
         return;
      }
      current = current[versionPath[i]];
    }
    
    const lastKey = versionPath[versionPath.length - 1];
    if (current[lastKey] !== undefined) {
        current[lastKey] = newVersion;
        await write(filePath, JSON.stringify(content, null, 2) + "\n");
        console.log(`Updated ${filePath}`);
    } else {
         console.error(`Key not found in ${filePath}: ${versionPath.join('.')}`);
    }

  } catch (error) {
    console.error(`Failed to update ${filePath}:`, error);
  }
}

async function updateTsFile(filePath: string) {
    try {
        const f = file(filePath);
        if (!(await f.exists())) {
            console.warn(`File not found: ${filePath}`);
            return;
        }
        let content = await f.text();
        // Regex to match: export const VERSION = '...';
        const regex = /export const VERSION = '.*';/;
        if (regex.test(content)) {
            content = content.replace(regex, `export const VERSION = '${newVersion}';`);
            await write(filePath, content);
            console.log(`Updated ${filePath}`);
        } else {
            console.warn(`Could not find VERSION constant in ${filePath}`);
        }
    } catch (error) {
        console.error(`Failed to update ${filePath}:`, error);
    }
}

// 1. package.json
await updateJsonFile("package.json", ["version"]);

// 2. ui/package.json
await updateJsonFile("ui/package.json", ["version"]);

// 3. tsoa.json
await updateJsonFile("tsoa.json", ["spec", "spec", "info", "version"]);

// 4. src/version/index.ts
await updateTsFile("src/version/index.ts");

console.log("Version bump complete!");
