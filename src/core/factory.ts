import fs from 'fs/promises'
import path from 'path'
import { decompressSync } from 'zstd-wasm-decoder'
import { ResourceLoader } from './loader.js'
import { FileSystem } from './interfaces.js'

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

    if (hash) {
      try {
        await fs.access(outputPath)
        return
      } catch {
      }
    }

    const data = await this.downloadBinary(url)
    await fs.writeFile(outputPath, data)
  }

  async decompress(inputPath: string, outputPath: string): Promise<void> {
    const compressedData = await fs.readFile(inputPath)
    const decompressed = decompressSync(new Uint8Array(compressedData))
    await fs.writeFile(outputPath, Buffer.from(decompressed))
  }

  private async downloadBinary(url: string): Promise<Buffer> {
    const controller = new AbortController()
    const timeoutId = setTimeout(() => controller.abort(), this.timeout)

    try {
      const response = await fetch(url, {
        signal: controller.signal,
        headers: {
          'User-Agent': 'MTranServer/4.0.0'
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

class NodeFileSystem implements FileSystem {
  async readFile(filePath: string): Promise<Buffer> {
    return fs.readFile(filePath)
  }

  async fileExists(filePath: string): Promise<boolean> {
    try {
      await fs.access(filePath)
      return true
    } catch {
      return false
    }
  }

  joinPath(...paths: string[]): string {
    return path.join(...paths)
  }
}

export function createResourceLoader(): ResourceLoader {
  return new ResourceLoader(new NodeFileSystem())
}

