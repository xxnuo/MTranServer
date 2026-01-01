import os from 'os'

export function getAvailableMemoryMB(): number {
  try {
    const freeMemBytes = os.freemem()
    return Math.floor(freeMemBytes / 1024 / 1024)
  } catch (error) {
    return 0
  }
}
