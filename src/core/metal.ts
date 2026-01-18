import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import type { ConvertedModel } from '../converter/cache.js';

export interface MetalInput { ids: number[]; candidates: number[] }

export class MetalWorker {
  ready: Promise<void>;
  private completed = 0;
  private child!: ChildProcessWithoutNullStreams;
  private iterator!: AsyncIterator<Buffer>;
  private buffer = Buffer.alloc(0);
  private queue: Promise<unknown> = Promise.resolve();
  private error?: Error;
  private closing?: Promise<void>;
  private exited!: Promise<void>;

  constructor(private executable: string, private model: string, private metallib: string) { this.ready = this.start(); }

  private start(): Promise<void> {
    this.buffer = Buffer.alloc(0); this.error = undefined;
    this.child = spawn(this.executable, [this.model, this.metallib], { stdio: ['pipe', 'pipe', 'pipe'] });
    this.iterator = this.child.stdout[Symbol.asyncIterator]();
    let stderr = '';
    this.child.stderr.on('data', data => { stderr = (stderr + String(data)).slice(-4096); });
    this.child.on('error', error => { this.error = error; });
    this.child.stdin.on('error', error => { this.error ??= error; });
    this.exited = new Promise(resolve => this.child.once('close', (code, signal) => {
      this.error ??= new Error(`Metal worker exited (${code ?? signal}): ${stderr}`);
      resolve();
    }));
    const timer = setTimeout(() => { this.error ??= new Error('Metal initialization timed out'); this.child.kill('SIGKILL'); }, 30_000);
    const ready = this.status().finally(() => clearTimeout(timer));
    ready.catch(() => this.child.kill());
    return ready;
  }

  private abort(message: string): never {
    this.error ??= new Error(message);
    this.child.kill('SIGKILL');
    throw this.error;
  }

  private async read(length: number): Promise<Buffer> {
    if (!Number.isSafeInteger(length) || length < 0 || length > 16 * 1024 * 1024) return this.abort('Invalid Metal response size');
    while (this.buffer.length < length) {
      if (this.error) throw this.error;
      const part = await this.iterator.next();
      if (part.done) throw this.error ?? new Error('Truncated Metal response');
      if (this.buffer.length + part.value.length > 16 * 1024 * 1024) return this.abort('Metal response exceeded buffer limit');
      this.buffer = Buffer.concat([this.buffer, part.value]);
    }
    const value = this.buffer.subarray(0, length);
    this.buffer = this.buffer.subarray(length);
    return value;
  }

  private async integer() { return (await this.read(4)).readUInt32LE(); }
  private async status() {
    const status = await this.integer();
    if (status === 0) return;
    if (status !== 1) return this.abort('Invalid Metal response status');
    const length = await this.integer();
    if (length > 8192) return this.abort('Metal error message exceeded limit');
    throw new Error((await this.read(length)).toString('utf8'));
  }

  translate(rows: MetalInput[], fast: boolean): Promise<number[][]> {
    if (this.closing) return Promise.reject(new Error('Metal worker is closing'));
    let payload: Buffer;
    try {
      if (!rows.length || rows.length > 32) throw new Error('Invalid Metal batch size');
      let length = 8;
      for (const row of rows) {
        if (!row.ids.length || row.ids.length > 512 || row.candidates.length > 262144) throw new Error('Invalid Metal input length');
        length += 8 + (row.ids.length + row.candidates.length) * 4;
      }
      payload = Buffer.allocUnsafe(length);
      let offset = 0;
      const integer = (value: number) => {
        if (!Number.isSafeInteger(value) || value < 0 || value > 0x7fff_ffff) throw new Error('Invalid Metal input integer');
        payload.writeUInt32LE(value, offset); offset += 4;
      };
      integer(rows.length); integer(Number(fast));
      for (const row of rows) {
        integer(row.ids.length); row.ids.forEach(integer);
        integer(row.candidates.length); row.candidates.forEach(integer);
      }
    } catch (error) { return Promise.reject(error); }
    const task = this.queue.then(async () => {
      // Compiled GPU graphs specialize on shapes; recycle to bound a long-lived model's cache.
      if (this.completed === 128) { await this.stop(); this.completed = 0; this.ready = this.start(); }
      await this.ready;
      if (this.error) throw this.error;
      const timer = setTimeout(() => { this.error ??= new Error('Metal translation timed out'); this.child.kill('SIGKILL'); }, 30_000);
      try {
        await new Promise<void>((resolve, reject) => this.child.stdin.write(payload, error => error ? reject(error) : resolve()));
        await this.status();
        if (await this.integer() !== rows.length) return this.abort('Metal result count mismatch');
        const results: number[][] = [];
        for (const row of rows) {
          const length = await this.integer();
          if (length > row.ids.length * 3) return this.abort('Metal generation limit exceeded');
          const bytes = await this.read(length * 4);
          const tokens = Array.from({ length }, (_, i) => bytes.readInt32LE(i * 4));
          if (tokens.some(token => token < 0 || token >= 262144)) return this.abort('Invalid Metal output token');
          results.push(tokens);
        }
        this.completed++;
        return results;
      } catch (error) { this.error ??= error instanceof Error ? error : new Error(String(error)); this.child.kill(); throw error; }
      finally { clearTimeout(timer); }
    });
    this.queue = task.catch(() => {});
    return task;
  }

  async translateMany(rows: MetalInput[], fast: boolean): Promise<number[][]> {
    const order = rows.map((_, index) => index).sort((a, b) => rows[b].ids.length - rows[a].ids.length);
    const results: number[][] = new Array(rows.length);
    for (let begin = 0; begin < order.length;) {
      const size = Math.min(32, order.length - begin, Math.floor(4096 / rows[order[begin]].ids.length));
      if (size < 1) throw new Error('Metal input exceeds padded token budget');
      const indices = order.slice(begin, begin + size);
      const tokens = await this.translate(indices.map(index => rows[index]), fast);
      indices.forEach((index, i) => { results[index] = tokens[i]; });
      begin += size;
    }
    return results;
  }

  private async stop() {
    this.child.stdin.end();
    const timer = setTimeout(() => this.child.kill('SIGKILL'), 2000);
    try { await this.exited; } finally { clearTimeout(timer); }
  }

  close(): Promise<void> {
    return this.closing ??= this.queue.then(() => this.stop());
  }
}

export function getMetalRuntime(model: ConvertedModel): string | undefined {
  if (process.platform !== 'darwin' || process.arch !== 'arm64' || process.env.MT_CPU_ONLY === 'true' || !model.sourceModel || !model.sourceModelSha256 || !/^[a-f0-9]{64}$/.test(model.sourceModelSha256)) return;
  return [new URL('./metal/', import.meta.url), new URL('../../dist/metal/', import.meta.url)]
    .map(url => fileURLToPath(url)).find(directory => ['metal-worker', 'mlx.metallib', 'libmlx.dylib', 'libjaccl.dylib'].every(file => existsSync(path.join(directory, file))));
}

export async function createMetalWorker(model: ConvertedModel, sourceEos: number, targetEos: number): Promise<MetalWorker | undefined> {
  const runtime = getMetalRuntime(model);
  if (!runtime) return;
  const { prepareMetalModel } = await import('../converter/metal.js');
  const filename = await prepareMetalModel(model, sourceEos, targetEos);
  const worker = new MetalWorker(path.join(runtime, 'metal-worker'), filename, path.join(runtime, 'mlx.metallib'));
  try { await worker.ready; return worker; }
  catch (error) { await worker.close(); throw error; }
}
