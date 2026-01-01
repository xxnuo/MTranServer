import { spawn } from 'child_process'
import fs from 'fs'
import path from 'path'

function run(command: string, args: string[]) {
  return new Promise<void>((resolve, reject) => {
    const proc = spawn(command, args, { stdio: 'inherit', env: process.env })
    proc.on('exit', (code) => {
      if (code === 0) resolve()
      else reject(new Error(`exit ${code}`))
    })
  })
}

async function main() {
  const bunBin = process.execPath
  await run(bunBin, ['run', 'build:lib'])

  const distDir = path.join(process.cwd(), 'dist')
  if (fs.existsSync(distDir)) {
    for (const entry of fs.readdirSync(distDir)) {
      if (entry.startsWith('mtranserver') || entry.endsWith('.exe')) {
        try {
          fs.rmSync(path.join(distDir, entry), { recursive: true, force: true })
        } catch {
          continue
        }
      }
    }
  }

  const platform = process.platform
  const builderArgs = ['node_modules/.bin/electron-builder']
  if (platform === 'linux') {
    builderArgs.push('--linux', 'AppImage')
  } else if (platform === 'darwin') {
    builderArgs.push('--mac')
  } else if (platform === 'win32') {
    builderArgs.push('--win')
  }

  await run(bunBin, builderArgs)
}

main().catch(() => process.exit(1))
