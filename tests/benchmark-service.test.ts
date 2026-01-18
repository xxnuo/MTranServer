import { expect, test } from 'bun:test';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';

test('service benchmark refuses cached results and records uncached server versions', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'mtran-benchmark-'));
  let cacheSize = 1000;
  let translations = 0;
  const server = Bun.serve({
    port: 0,
    fetch(request) {
      if (new URL(request.url).pathname.endsWith('/ui/api/settings')) {
        return Response.json({ version: '5.0.0', config: { server: { cacheSize } } });
      }
      translations++;
      return Response.json({ result: 'Hello' });
    },
  });
  try {
    const corpus = path.join(root, 'corpus.jsonl');
    const output = path.join(root, 'results.json');
    await writeFile(corpus, JSON.stringify({ source: 'Hallo' }) + '\n');
    const baseline = new URL('baseline', server.url).href;
    const candidate = new URL('candidate', server.url).href;
    const run = async () => {
      const child = Bun.spawn([process.execPath, 'scripts/benchmark-service.ts', corpus, output, baseline, candidate], { stdout: 'pipe', stderr: 'pipe' });
      const [code, , stderr] = await Promise.all([child.exited, new Response(child.stdout).text(), new Response(child.stderr).text()]);
      return { code, stderr };
    };
    const cached = await run();
    expect(cached.code).not.toBe(0);
    expect(cached.stderr).toContain('MT_CACHE_SIZE=0');
    expect(translations).toBe(0);
    cacheSize = 0;
    expect((await run()).code).toBe(0);
    expect(translations).toBe(8);
    const result = JSON.parse(await readFile(output, 'utf8'));
    expect(result.servers[candidate]).toEqual({ version: '5.0.0', cacheSize: 0 });
    expect(result.results[candidate].rounds).toHaveLength(3);
  } finally {
    server.stop(true);
    await rm(root, { recursive: true, force: true });
  }
}, 15000);
