import fs from 'fs/promises'
import { createHash } from 'node:crypto'
import { decompress } from 'fzstd'
import { VERSION } from '@/version/index.js'

export interface DownloadOptions {
  url: string
  outputPath: string
  hash?: string
}

export class Downloader {
  private timeout: number

  constructor(timeout: number = 1800000) {
    this.timeout = timeout
  }

  async download(options: DownloadOptions): Promise<void> {
    const { url, outputPath, hash } = options

    const data = await this.downloadBinary(url)
    if (hash && createHash('sha256').update(data).digest('hex') !== hash) throw new Error('Download hash mismatch')
    await fs.writeFile(outputPath, data)
  }

  async decompress(inputPath: string, outputPath: string): Promise<void> {
    const compressedData = await fs.readFile(inputPath)
    const decompressed = decompress(compressedData)
    await fs.writeFile(outputPath, decompressed)
  }

  private async downloadBinary(url: string): Promise<Buffer> {
    const controller = new AbortController()
    const timeoutId = setTimeout(() => controller.abort(), this.timeout)

    try {
      const response = await fetch(url, {
        signal: controller.signal,
        headers: {
          'User-Agent': `MTranServer/${VERSION}`
        }
      })

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`)
      }

      const arrayBuffer = await response.arrayBuffer()
      return Buffer.from(arrayBuffer)
    } finally {
      clearTimeout(timeoutId)
    }
  }
}

export function createDownloader(): Downloader {
  return new Downloader()
}

