// Regenerate after updating the pinned official ONNX v1.18.0 schema (Apache-2.0).
import protobuf from 'protobufjs';
import { writeFile } from 'node:fs/promises';
const root = await protobuf.load(new URL('../src/converter/onnx.proto', import.meta.url).pathname);
await writeFile(new URL('../src/converter/schema.json', import.meta.url), JSON.stringify(root.toJSON()));
