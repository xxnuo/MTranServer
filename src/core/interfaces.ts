
export interface FileSystem {
  readFile(filepath: string): Promise<Buffer>;
  fileExists(filepath: string): Promise<boolean>;
  joinPath(...paths: string[]): string;
}

export interface BergamotModule {
  wasmBinary?: ArrayBuffer | Buffer;
  print?: (msg: string) => void;
  printErr?: (msg: string) => void;
  onRuntimeInitialized?: () => void;
  onAbort?: (msg: string) => void;
  AlignedMemory: any;
  AlignedMemoryList: any;
  TranslationModel: any;
  BlockingService: any;
  VectorString: any;
  VectorResponseOptions: any;
}
