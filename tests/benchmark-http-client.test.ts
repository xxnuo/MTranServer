import { test, expect } from 'bun:test';
import { createServer } from 'node:http';
import path from 'node:path';

test('HTTP benchmark client preserves request order, Unicode and actual parallelism', async () => {
  let active = 0, peak = 0;
  const server = createServer(async (request, response) => {
    let body = ''; for await (const chunk of request) body += chunk;
    const payload = JSON.parse(body); active++; peak = Math.max(peak, active);
    await new Promise(resolve => setTimeout(resolve, 15));
    active--; response.setHeader('Content-Type', 'application/json'); response.end(JSON.stringify({ result: payload.text }));
  });
  await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
  const address = server.address() as { port: number };
  try {
    const process = Bun.spawn(['node', path.resolve('scripts/benchmark-http-client.mjs')], { stdin: 'pipe', stdout: 'pipe', stderr: 'pipe' });
    process.stdin.write(JSON.stringify({ url: `http://127.0.0.1:${address.port}`, concurrency: 3, field: 'result', payloads: Array.from({ length: 9 }, (_, i) => ({ text: `句子${i}` })) })); process.stdin.end();
    const [out, err, exit] = await Promise.all([new Response(process.stdout).text(), new Response(process.stderr).text(), process.exited]);
    expect(exit, err).toBe(0); const result = JSON.parse(out);
    expect(result.measured.map((row: [number, string]) => row[1])).toEqual(Array.from({ length: 9 }, (_, i) => `句子${i}`));
    expect(result.measured.every((row: [number, string]) => row[0] >= 0)).toBe(true);
    expect(result.wall).toBeGreaterThan(0); expect(peak).toBe(3);
  } finally { server.closeAllConnections(); await new Promise<void>(resolve => server.close(() => resolve())); }
});
