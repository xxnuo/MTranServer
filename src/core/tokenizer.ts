import { readFile } from 'node:fs/promises';
import { SentencePieceProcessor } from '@sctg/sentencepiece-js';
import { vocabularyInfo } from './shortlist.js';

export class Tokenizer {
  private constructor(private processor: SentencePieceProcessor, readonly eos: number, readonly size: number) {}
  static async load(file: string) {
    const bytes = await readFile(file);
    const { eos, size: pieces } = vocabularyInfo(bytes);
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
