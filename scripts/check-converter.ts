import { mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { readRelease } from '../src/converter/release.js';
import { buildModel, type Mode } from '../src/converter/graph.js';

const [model, output, selected] = process.argv.slice(2);
if (!output) throw new Error('Usage: check-converter release.bin output-directory');
await mkdir(output, { recursive: true });
const { weights, config } = await readRelease(model);
for (const mode of (selected ? [selected as Mode] : ['fp32', 'qall'] as const)) {
  const started = performance.now();
  await writeFile(path.join(output, mode + '.onnx'), buildModel(weights, config, mode));
  console.log(JSON.stringify({ mode, seconds: (performance.now() - started) / 1000, peakRssMiB: process.resourceUsage().maxRSS / 1024 }));
}
