import { FileSystem, BergamotModule } from '@/core/interfaces.js';

export interface ModelFileNames {
  model?: string;
  lex?: string;
  srcvocab?: string;
  trgvocab?: string;
}

export interface ModelBuffers {
  model: Buffer;
  lex: Buffer;
  srcvocab: Buffer;
  trgvocab: Buffer;
  qualityModel?: Buffer;
}

export class ResourceLoader {
  constructor(private fileSystem: FileSystem) {}

  async loadBergamotModule(wasmBinary: ArrayBuffer | Buffer, loadBergamot: any): Promise<BergamotModule> {
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        reject(new Error('WASM initialization timeout'));
      }, 30000);

      console.log('[Bergamot] Loading WASM, binary size:', wasmBinary.byteLength || wasmBinary.length);

      loadBergamot({
        wasmBinary: wasmBinary,
        print: (msg: string) => console.log(`[Bergamot]: ${msg}`),
        printErr: (msg: string) => console.error(`[Bergamot Error]: ${msg}`),
        onRuntimeInitialized: function(this: BergamotModule) {
          console.log('[Bergamot] Runtime initialized successfully');
          clearTimeout(timeout);
          resolve(this);
        },
        onAbort: (msg: string) => {
          console.error('[Bergamot] Aborted:', msg);
          clearTimeout(timeout);
          reject(new Error(`WASM aborted: ${msg}`));
        }
      });
    });
  }

  async loadModelFiles(modelPath: string, fileNames: ModelFileNames | null = null): Promise<ModelBuffers> {
    const defaultFiles: Required<ModelFileNames> = {
      model: 'model.enzh.intgemm.alphas.bin',
      lex: 'lex.50.50.enzh.s2t.bin',
      srcvocab: 'srcvocab.enzh.spm',
      trgvocab: 'trgvocab.enzh.spm'
    };

    const files = { ...defaultFiles, ...fileNames };
    const buffers: Partial<ModelBuffers> = {};

    for (const [key, filename] of Object.entries(files)) {
      const filepath = this.fileSystem.joinPath(modelPath, filename);
      if (await this.fileSystem.fileExists(filepath)) {
        buffers[key as keyof ModelBuffers] = await this.fileSystem.readFile(filepath);
      } else {
        throw new Error(`Model file not found: ${filepath}`);
      }
    }

    return buffers as ModelBuffers;
  }

  async loadWasmBinary(wasmPath: string): Promise<Buffer> {
    return this.fileSystem.readFile(wasmPath);
  }
}
