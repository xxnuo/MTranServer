import { expect, test } from 'bun:test';
import { mkdtemp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';

test('ONNX benchmark rejects caching and records all real HTTP samples across isolated processes', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'mtran-onnx-test-'));
  try {
    const entry = path.join(root, 'server.mjs'), output = path.join(root, 'results.json');
    await writeFile(entry, `
      import http from 'node:http';
      const port = Number(process.argv[process.argv.indexOf('--port') + 1]);
      http.createServer(async (req, res) => {
        res.setHeader('content-type', 'application/json');
        if (req.url === '/health') return res.end('{}');
        if (req.url === '/ui/api/settings') return res.end(JSON.stringify({version:'test',config:{server:{cacheSize:Number(process.env.BENCH_TEST_CACHE)}},engines:[['en','zh-Hans'],['zh-Hans','en'],['de','en']].map(([from,to])=>({from,to,provider:'cpu',cpuThreads:1}))}));
        let body = ''; for await (const chunk of req) body += chunk;
        const data = JSON.parse(body);
        res.end(JSON.stringify({result:data.to + ': ' + data.text}));
      }).listen(port, '127.0.0.1');
    `);
    for (const pair of ['en_zh-Hans', 'zh-Hans_en', 'de_en']) {
      const dir = path.join(root, 'models', pair);
      await mkdir(dir, { recursive: true });
      await writeFile(path.join(dir, 'manifest.json'), JSON.stringify({ records: [] }));
      await writeFile(path.join(dir, 'model.onnx'), 'mock model');
    }
    const run = async (cache: number) => {
      const child = Bun.spawn([process.execPath, 'scripts/benchmark-onnx.ts', entry, path.join(root, 'models'), output], {
        env: { ...process.env, BENCH_TEST_CACHE: String(cache) }, stdout: 'pipe', stderr: 'pipe',
      });
      const [code, , error] = await Promise.all([child.exited, new Response(child.stdout).text(), new Response(child.stderr).text()]);
      return { code, error };
    };
    const cached = await run(1000);
    expect(cached.code).not.toBe(0);
    expect(cached.error).toContain('MT_CACHE_SIZE=0');
    const uncached = await run(0);
    expect(uncached.code, uncached.error).toBe(0);
    const report = JSON.parse(await readFile(output, 'utf8'));
    expect(report.finishedAt).not.toBe('');
    expect(report.results).toHaveLength(3);
    for (const result of report.results) {
      expect(result.serverSettings.cacheSize).toBe(0);
      expect(result.runtime.provider).toBe('cpu');
      expect(result.cases.map((c: any) => c.requests)).toEqual([96, 96, 24]);
      for (const c of result.cases) {
        expect(c.measurements).toHaveLength(3);
        expect(c.summary.p95Ms).toBeGreaterThanOrEqual(c.summary.p50Ms);
        expect(c.summary.peakRssMiB).toBeGreaterThan(0);
        expect(c.measurements.flatMap((r: any) => r.samples)).toHaveLength(c.requests);
      }
    }
  } finally {
    await rm(root, { recursive: true, force: true });
  }
}, 30000);
