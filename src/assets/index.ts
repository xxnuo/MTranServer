import fs from 'fs/promises'
import path from 'path'
import { fileURLToPath } from 'url'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)

export function getEmbeddedAssetPath(filename: string): string {
  const assetsDir = path.resolve(__dirname, '../../assets')
  return path.join(assetsDir, filename)
}

export async function cleanupLegacyBin(configDir: string): Promise<void> {
  const binDir = path.join(configDir, 'bin')
  try {
    const stat = await fs.stat(binDir)
    if (stat.isDirectory()) {
      await fs.rm(binDir, { recursive: true, force: true })
    }
  } catch {
  }
}
