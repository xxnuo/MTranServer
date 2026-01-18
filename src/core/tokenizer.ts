import { readFile } from 'node:fs/promises';
import { SentencePieceProcessor } from '@sctg/sentencepiece-js';
import protobuf from 'protobufjs';

export class Tokenizer {
  private constructor(private processor: SentencePieceProcessor, readonly eos: number, readonly size: number) {}
  static async load(file: string) {
    const bytes = await readFile(file), reader = protobuf.Reader.create(bytes);
    let eos = 2, pieces = 0;
    while (reader.pos < reader.len) {
      const tag = reader.uint32();
      if (tag === 10) { pieces++; reader.skipType(2); }
      else if (tag === 18) {
        const length = reader.uint32(), end = reader.pos + length;
        if (end > reader.len) throw new Error('Truncated SentencePiece trainer spec');
        while (reader.pos < end) { const t = reader.uint32(); if (t === 336) eos = reader.int32(); else reader.skipType(t & 7); }
      } else reader.skipType(tag & 7);
    }
    if (eos < 0 || pieces <= eos) throw new Error('Invalid SentencePiece vocabulary or EOS token');
    const processor = new SentencePieceProcessor();
    try {
      await processor.loadFromB64StringModel(bytes.toString('base64'));
      const status = processor.processor.status();
      try { const message = status.ToString(); if (message !== 'OK') throw new Error(`SentencePiece load failed: ${message}`); }
      finally { status.delete(); }
      return new Tokenizer(processor, eos, pieces);
    }
    catch (error) { processor.processor?.delete(); throw error; }
  }
  encode(text: string) { return this.processor.encodeIds(text); }
  decode(ids: number[]) { return this.processor.decodeIds(Int32Array.from(ids)) as string; }
  close() { this.processor.processor?.delete(); }
}
