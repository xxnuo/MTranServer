import crypto from 'crypto'
import fs from 'fs/promises'
import http from 'http'
import https from 'https'
import path from 'path'
import type { IncomingMessage } from 'http'
import { decompress } from 'fzstd'
import { ProxyAgent } from 'proxy-agent'
import { ResourceLoader } from '@/core/loader.js'
import { FileSystem } from '@/core/interfaces.js'

export interface DownloadOptions {
  url: string
  urls?: string[]
  outputPath: string
  hash?: string
  proxy?: string
  signal?: AbortSignal
  onProgress?: (progress: DownloadProgress) => void
  onFallback?: (failedUrl: string, nextUrl: string) => void
}

export interface DownloadProgress {
  downloadedBytes: number
  totalBytes?: number
}

export interface SpeedTestOptions {
  url: string
  proxy?: string
  maxBytes?: number
}

export interface SpeedTestResult {
  latencyMs: number
  downloadedBytes: number
  durationMs: number
  speedMbps: number
}

export class DownloadCancelledError extends Error {
  constructor() {
    super('Download cancelled')
    this.name = 'DownloadCancelledError'
  }
}

export class Downloader {
  private timeout: number

  constructor(timeout: number = 1800000) {
    this.timeout = timeout
  }

  async download(options: DownloadOptions): Promise<void> {
    const { url, urls = [], outputPath, hash, proxy, signal, onProgress, onFallback } = options
    this.throwIfAborted(signal)

    if (hash) {
      try {
        if ((await this.hashFile(outputPath)).toLowerCase() === hash.toLowerCase()) {
          return
        }
      } catch {
      }
    }

    const candidates = [url, ...urls].filter((candidate, index, all) => candidate && all.indexOf(candidate) === index)
    let lastError: unknown
    for (let index = 0; index < candidates.length; index++) {
      const candidate = candidates[index]
      try {
        await this.downloadToFile(candidate, outputPath, hash, proxy, signal, onProgress)
        return
      } catch (error) {
        if (signal?.aborted) throw new DownloadCancelledError()
        lastError = error
        const nextCandidate = candidates[index + 1]
        if (nextCandidate) onFallback?.(candidate, nextCandidate)
        console.warn(`Download source failed: ${candidate}`, error)
      }
    }

    throw lastError instanceof Error ? lastError : new Error('Download failed')
  }

  async decompress(inputPath: string, outputPath: string): Promise<void> {
    const compressedData = await fs.readFile(inputPath)
    const decompressed = decompress(compressedData)
    await fs.writeFile(outputPath, decompressed)
  }

  async verifyHash(filePath: string, expectedHash: string): Promise<boolean> {
    return (await this.hashFile(filePath)).toLowerCase() === expectedHash.toLowerCase()
  }

  async measureSpeed(options: SpeedTestOptions): Promise<SpeedTestResult> {
    const maxBytes = Math.max(1, options.maxBytes || 10 * 1024 * 1024)
    const startedAt = performance.now()
    const response = await this.request(options.url, {
      'User-Agent': 'MTranServer-speed-test/1.0',
      Range: `bytes=0-${maxBytes - 1}`,
    }, options.proxy)
    const latencyMs = Math.max(0, Math.round(performance.now() - startedAt))
    const status = response.statusCode || 0
    if (status < 200 || status >= 300) {
      response.resume()
      throw new Error(`HTTP ${status}: ${response.statusMessage || 'Speed test failed'}`)
    }

    const bodyStartedAt = performance.now()
    let downloadedBytes = 0
    for await (const chunk of response) {
      const chunkBytes = Buffer.from(chunk as Uint8Array)
      downloadedBytes += Math.min(chunkBytes.length, maxBytes - downloadedBytes)
      if (downloadedBytes >= maxBytes) {
        response.destroy()
        break
      }
    }
    const durationMs = Math.max(1, Math.round(performance.now() - bodyStartedAt))
    const speedMbps = downloadedBytes * 8 / (durationMs / 1000) / 1_000_000
    return { latencyMs, downloadedBytes, durationMs, speedMbps }
  }

  async measureLatency(options: SpeedTestOptions): Promise<number> {
    const startedAt = performance.now()
    const response = await this.request(options.url, {
      'User-Agent': 'MTranServer-latency-test/1.0',
      Range: 'bytes=0-0',
    }, options.proxy)
    const status = response.statusCode || 0
    response.resume()
    if (status < 200 || status >= 300) {
      throw new Error(`HTTP ${status}: ${response.statusMessage || 'Latency test failed'}`)
    }
    return Math.max(0, Math.round(performance.now() - startedAt))
  }

  async fetchJson<T>(url: string, proxy?: string): Promise<T> {
    const response = await this.request(url, {
      Accept: 'application/json',
      'User-Agent': 'MTranServer/4.0.0',
    }, proxy)
    const status = response.statusCode || 0
    if (status < 200 || status >= 300) {
      response.resume()
      throw new Error(`HTTP ${status}: ${response.statusMessage || 'Request failed'}`)
    }

    const chunks: Buffer[] = []
    let bodySize = 0
    for await (const chunk of response) {
      const buffer = Buffer.from(chunk as Uint8Array)
      bodySize += buffer.length
      if (bodySize > 4 * 1024 * 1024) {
        response.destroy()
        throw new Error('JSON response is too large')
      }
      chunks.push(buffer)
    }
    return JSON.parse(Buffer.concat(chunks).toString('utf8')) as T
  }

  private async downloadToFile(
    url: string,
    outputPath: string,
    hash: string | undefined,
    proxy: string | undefined,
    signal: AbortSignal | undefined,
    onProgress?: (progress: DownloadProgress) => void
  ): Promise<void> {
    const partialPath = `${outputPath}.part`
    await fs.mkdir(path.dirname(path.resolve(outputPath)), { recursive: true })

    let lastError: unknown
    for (let attempt = 1; attempt <= 3; attempt++) {
      try {
        this.throwIfAborted(signal)
        let existingBytes = await this.getFileSize(partialPath)
        const headers: Record<string, string> = {
          'User-Agent': 'MTranServer/4.0.0',
        }
        if (existingBytes > 0) {
          headers.Range = `bytes=${existingBytes}-`
        }

        const response = await this.request(url, headers, proxy, signal)
        const status = response.statusCode || 0
        if (status === 416 && existingBytes > 0) {
          response.resume()
          await fs.rm(partialPath, { force: true })
          continue
        }
        if (status < 200 || status >= 300) {
          response.resume()
          throw new Error(`HTTP ${status}: ${response.statusMessage || 'Download failed'}`)
        }

        if (existingBytes > 0 && status !== 206) {
          existingBytes = 0
          await fs.rm(partialPath, { force: true })
        }
        if (existingBytes > 0 && this.getRangeStart(response) !== existingBytes) {
          response.resume()
          await fs.rm(partialPath, { force: true })
          throw new Error('Server returned an invalid resume range')
        }

        const totalBytes = this.getTotalBytes(response, existingBytes)
        const file = await fs.open(partialPath, existingBytes > 0 ? 'a' : 'w')
        let downloadedBytes = existingBytes
        try {
          for await (const chunk of response) {
            this.throwIfAborted(signal)
            const buffer = Buffer.from(chunk as Uint8Array)
            await file.write(buffer)
            downloadedBytes += buffer.length
            onProgress?.({ downloadedBytes, totalBytes })
          }
        } finally {
          await file.close()
        }

        if (totalBytes !== undefined && downloadedBytes < totalBytes) {
          throw new Error(`Download incomplete: ${downloadedBytes}/${totalBytes} bytes`)
        }
        if (hash && (await this.hashFile(partialPath)).toLowerCase() !== hash.toLowerCase()) {
          await fs.rm(partialPath, { force: true })
          throw new Error('Downloaded file hash mismatch')
        }

        await fs.rm(outputPath, { force: true })
        await fs.rename(partialPath, outputPath)
        return
      } catch (error) {
        if (signal?.aborted) throw new DownloadCancelledError()
        lastError = error
        if (attempt < 3) {
          await new Promise(resolve => setTimeout(resolve, attempt * 1000))
        }
      }
    }

    throw lastError instanceof Error ? lastError : new Error('Download failed')
  }

  private request(url: string, headers: Record<string, string>, proxy?: string, signal?: AbortSignal): Promise<IncomingMessage> {
    const target = new URL(url)
    if (target.protocol !== 'http:' && target.protocol !== 'https:') {
      return Promise.reject(new Error(`Unsupported download protocol: ${target.protocol}`))
    }

    const transport = target.protocol === 'https:' ? https : http
    const agent = proxy ? new ProxyAgent({ getProxyForUrl: () => proxy }) : undefined
    return new Promise((resolve, reject) => {
      let settled = false
      let response: IncomingMessage | undefined
      let request: ReturnType<typeof transport.get>

      const cleanupAbortListener = () => {
        signal?.removeEventListener('abort', abortRequest)
      }
      const abortRequest = () => {
        const error = new DownloadCancelledError()
        // AbortController 不一定能中断 Bun 的响应流，这里把请求和响应都主动销毁。
        response?.destroy(error)
        request.destroy(error)
        if (!settled) {
          settled = true
          reject(error)
        }
      }

      request = transport.get({
        protocol: target.protocol,
        hostname: target.hostname,
        port: target.port || undefined,
        path: `${target.pathname}${target.search}`,
        headers,
        agent,
      }, incomingResponse => {
        response = incomingResponse
        incomingResponse.once('close', cleanupAbortListener)
        if (signal?.aborted) {
          abortRequest()
          return
        }
        const status = incomingResponse.statusCode || 0
        const location = incomingResponse.headers.location
        if (status >= 300 && status < 400 && location) {
          incomingResponse.resume()
          this.request(new URL(location, target).toString(), headers, proxy, signal).then(resolve, reject)
          return
        }
        settled = true
        resolve(incomingResponse)
      })
      request.setTimeout(this.timeout, () => request.destroy(new Error('Download request timeout')))
      request.on('error', error => {
        cleanupAbortListener()
        if (!settled) {
          settled = true
          reject(error)
        }
      })
      signal?.addEventListener('abort', abortRequest, { once: true })
      if (signal?.aborted) abortRequest()
    })
  }

  private getTotalBytes(response: IncomingMessage, existingBytes: number): number | undefined {
    const contentRange = response.headers['content-range']
    if (typeof contentRange === 'string') {
      const match = contentRange.match(/\/(\d+)$/)
      if (match) return Number(match[1])
    }
    const contentLength = response.headers['content-length']
    if (typeof contentLength === 'string' && Number.isFinite(Number(contentLength))) {
      return existingBytes + Number(contentLength)
    }
    return undefined
  }

  private getRangeStart(response: IncomingMessage): number | undefined {
    const contentRange = response.headers['content-range']
    if (typeof contentRange !== 'string') return undefined
    const match = contentRange.match(/^bytes (\d+)-\d+\/\d+$/)
    return match ? Number(match[1]) : undefined
  }

  private throwIfAborted(signal?: AbortSignal): void {
    if (signal?.aborted) throw new DownloadCancelledError()
  }

  private async getFileSize(filePath: string): Promise<number> {
    try {
      return (await fs.stat(filePath)).size
    } catch {
      return 0
    }
  }

  private async hashFile(filePath: string): Promise<string> {
    const digest = crypto.createHash('sha256')
    const file = await fs.open(filePath, 'r')
    try {
      for await (const chunk of file.readableWebStream()) {
        digest.update(Buffer.from(chunk as Uint8Array))
      }
    } finally {
      await file.close()
    }
    return digest.digest('hex')
  }
}

export function createDownloader(timeout?: number): Downloader {
  return new Downloader(timeout)
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

