import http from 'node:http';
import assert from 'node:assert/strict';

let raw = '';
for await (const chunk of process.stdin) raw += chunk;
const config = JSON.parse(raw);
assert(Number.isSafeInteger(config.concurrency) && config.concurrency >= 1 && config.concurrency <= 256);
assert(Array.isArray(config.payloads) && config.payloads.length > 0);
assert(new URL(config.url).protocol === 'http:');
assert(['result', 'text'].includes(config.field));
const agent = new http.Agent({ keepAlive: true, maxSockets: config.concurrency });
const results = new Array(config.payloads.length);
let next = 0;

function request(payload) {
  return new Promise((resolve, reject) => {
    const body = JSON.stringify(payload), start = performance.now();
    const req = http.request(config.url + '/translate', {
      method: 'POST', agent,
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body) },
    }, response => {
      let text = '';
      response.setEncoding('utf8');
      response.on('error', reject);
      response.on('data', chunk => text += chunk);
      response.on('end', () => {
        try {
          assert.equal(response.statusCode, 200, text);
          const output = JSON.parse(text)[config.field];
          assert(typeof output === 'string' && output.length);
          resolve([performance.now() - start, output]);
        } catch (error) { reject(error); }
      });
    });
    req.on('error', reject);
    req.setTimeout(180000, () => req.destroy(new Error('timeout')));
    req.end(body);
  });
}

const start = performance.now();
try {
  await Promise.all(Array.from({ length: config.concurrency }, async () => {
    while (next < config.payloads.length) {
      const index = next++;
      results[index] = await request(config.payloads[index]);
    }
  }));
  console.log(JSON.stringify({ wall: (performance.now() - start) / 1000, measured: results }));
} finally { agent.destroy(); }
