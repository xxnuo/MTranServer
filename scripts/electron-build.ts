import { spawn } from 'child_process'
import fs from 'fs'
import path from 'path'

function run(command: string, args: string[], options?: { cwd?: string; env?: NodeJS.ProcessEnv }) {
  return new Promise<void>((resolve, reject) => {
    const proc = spawn(command, args, {
      stdio: 'inherit',
      env: options?.env ?? process.env,
      cwd: options?.cwd
    })
    proc.on('exit', (code) => {
      if (code === 0) resolve()
      else reject(new Error(`exit ${code}`))
    })
  })
}

function cleanDistBinaries(distDir: string) {
  if (!fs.existsSync(distDir)) return
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

async function prepareNpmModules(root: string) {
  const tempDir = path.join(root, '.electron-build-temp')
  fs.rmSync(tempDir, { recursive: true, force: true })
  fs.mkdirSync(tempDir, { recursive: true })
  fs.copyFileSync(path.join(root, 'package.json'), path.join(tempDir, 'package.json'))
  if (fs.existsSync(path.join(root, 'package-lock.json'))) {
    fs.copyFileSync(path.join(root, 'package-lock.json'), path.join(tempDir, 'package-lock.json'))
  }
  await run('npm', ['install', '--omit=dev', '--ignore-scripts', '--install-strategy=hoisted'], { cwd: tempDir })
  const targetModules = path.join(root, 'node_modules_prod')
  fs.rmSync(targetModules, { recursive: true, force: true })
  fs.renameSync(path.join(tempDir, 'node_modules'), targetModules)
  fs.rmSync(tempDir, { recursive: true, force: true })
  return targetModules
}

function getUnpackedDir(root: string) {
  const platform = process.platform
  const releaseDir = path.join(root, 'release')
  if (platform === 'linux') {
    return path.join(releaseDir, 'linux-unpacked')
  } else if (platform === 'darwin') {
    const entries = fs.readdirSync(releaseDir).filter(e => e.endsWith('.app'))
    if (entries.length > 0) {
      return path.join(releaseDir, 'mac-arm64', entries[0])
    }
    return path.join(releaseDir, 'mac')
  } else if (platform === 'win32') {
    return path.join(releaseDir, 'win-unpacked')
  }
  return path.join(releaseDir, 'linux-unpacked')
}

async function repackAsar(unpackedDir: string) {
  const appDir = path.join(unpackedDir, 'resources', 'app')
  const asarFile = path.join(unpackedDir, 'resources', 'app.asar')
  const asarUnpackDir = path.join(unpackedDir, 'resources', 'app.asar.unpacked')
  const binDir = path.join(appDir, 'node_modules', '.bin')
  if (fs.existsSync(binDir)) {
    fs.rmSync(binDir, { recursive: true })
  }
  if (fs.existsSync(asarFile)) {
    fs.rmSync(asarFile)
  }
  if (fs.existsSync(asarUnpackDir)) {
    fs.rmSync(asarUnpackDir, { recursive: true })
  }
  await run('npx', ['--yes', '@electron/asar', 'pack', appDir, asarFile])
  fs.rmSync(appDir, { recursive: true })
}

async function main() {
  const bunBin = process.execPath
  const root = process.cwd()

  await run(bunBin, ['run', 'build:lib'])

  const distDir = path.join(root, 'dist')
  cleanDistBinaries(distDir)

  console.log('Installing production dependencies with npm...')
  const prodModules = await prepareNpmModules(root)

  const originalModules = path.join(root, 'node_modules')
  const backupModules = path.join(root, 'node_modules_bun')

  fs.renameSync(originalModules, backupModules)
  fs.renameSync(prodModules, originalModules)

  const electronBuilderPath = path.join(backupModules, '.bin/electron-builder')
  const env = { ...process.env, NODE_PATH: backupModules }

  try {
    const platform = process.platform
    const dirArgs = [electronBuilderPath, '--dir']
    if (process.argv.includes('--all')) {
      dirArgs.push('-mwl')
    } else if (platform === 'linux') {
      dirArgs.push('--linux')
    } else if (platform === 'darwin') {
      dirArgs.push('--mac')
    } else if (platform === 'win32') {
      dirArgs.push('--win')
    }

    console.log('Packaging directory...')
    await run(bunBin, dirArgs, { env })

    const unpackedDir = getUnpackedDir(root)
    const appNodeModules = path.join(unpackedDir, 'resources', 'app', 'node_modules')

    console.log('Copying all node_modules...')
    fs.rmSync(appNodeModules, { recursive: true, force: true })
    fs.cpSync(originalModules, appNodeModules, { recursive: true })

    console.log('Repacking asar...')
    await repackAsar(unpackedDir)

    console.log('Building final package...')
    const finalArgs = [electronBuilderPath, '--prepackaged', unpackedDir]
    if (process.argv.includes('--all')) {
      finalArgs.push('-mwl')
    } else if (platform === 'linux') {
      finalArgs.push('--linux', 'AppImage')
    } else if (platform === 'darwin') {
      finalArgs.push('--mac')
    } else if (platform === 'win32') {
      finalArgs.push('--win')
    }
    await run(bunBin, finalArgs, { env })
  } finally {
    fs.rmSync(originalModules, { recursive: true, force: true })
    fs.renameSync(backupModules, originalModules)
  }
}

main().catch(() => process.exit(1))
