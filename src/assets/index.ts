import fs from 'fs/promises'
import path from 'path'

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
