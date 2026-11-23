import { readFile } from 'node:fs/promises';
import { SentencePieceProcessor } from '@sctg/sentencepiece-js';
import { vocabularyInfo } from './shortlist.js';

export class Tokenizer {
  private refs = 1;
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
  decode(ids: number[] | Int32Array) { return this.processor.decodeIds(ids instanceof Int32Array ? ids : Int32Array.from(ids)) as string; }
  retain() {
    if (this.refs <= 0) throw new Error('Tokenizer already closed');
    this.refs++;
    return this;
  }
  close() {
    if (this.refs <= 0) return;
    if (--this.refs === 0) this.processor.processor?.delete();
  }
}
