import { spawn } from 'child_process'
import fs from 'fs'
import path from 'path'

const root = process.cwd()
const electronBin = path.join(root, 'node_modules', '.bin', process.platform === 'win32' ? 'electron.cmd' : 'electron')
const bunBin = process.execPath

function run(command: string, args: string[]) {
  return spawn(command, args, { stdio: 'inherit', env: process.env })
}

function runOnce(command: string, args: string[]) {
  return new Promise<void>((resolve, reject) => {
    const proc = run(command, args)
    proc.on('exit', (code) => {
      if (code === 0) resolve()
      else reject(new Error(`exit ${code}`))
    })
  })
}

async function runBuildOnce() {
  await runOnce(bunBin, ['run', 'build:lib'])
}

function startBuildWatch() {
  const args = [
    'build',
    'src/index.ts',
    'src/main.ts',
    'src/desktop-server.ts',
    '--outdir',
    'dist',
    '--target',
    'node',
    '--format',
    'esm',
    '--sourcemap',
    '--external',
    'zstd-wasm-decoder',
    '--external',
    'express',
    '--watch'
  ]
  return run(bunBin, args)
}

let electronProcess: ReturnType<typeof run> | null = null
let restartTimer: NodeJS.Timeout | null = null
let restarting = false

function startElectron() {
  if (electronProcess) return
  electronProcess = run(electronBin, ['./electron-main.js'])
  electronProcess.on('exit', () => {
    electronProcess = null
  })
}

async function stopElectron() {
  if (!electronProcess) return
  const proc = electronProcess
  await new Promise<void>((resolve) => {
    proc.once('exit', () => resolve())
    proc.kill('SIGTERM')
    setTimeout(() => {
      if (!proc.killed) {
        proc.kill('SIGKILL')
      }
    }, 3000)
  })
}

function scheduleRestart() {
  if (restartTimer) clearTimeout(restartTimer)
  restartTimer = setTimeout(async () => {
    if (restarting) return
    restarting = true
    await stopElectron()
    startElectron()
    restarting = false
  }, 250)
}

function watchPath(target: string) {
  try {
    fs.watch(target, () => scheduleRestart())
  } catch {
    return
  }
}

function setupWatchers() {
  watchPath(path.join(root, 'dist'))
  watchPath(path.join(root, 'desktop'))
  watchPath(path.join(root, 'electron-main.js'))
}

async function main() {
  await runBuildOnce()
  const buildWatcher = startBuildWatch()
  startElectron()
  setupWatchers()

  const cleanup = async () => {
    await stopElectron()
    buildWatcher.kill('SIGTERM')
    process.exit(0)
  }

  process.on('SIGINT', cleanup)
  process.on('SIGTERM', cleanup)
}

main().catch(() => process.exit(1))
