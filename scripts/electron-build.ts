import { spawn } from 'child_process'
import path from 'path'
import fs from 'fs'

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

async function main() {
  const bunBin = process.execPath
  const root = process.cwd()

  // Build for Node (bundles dependencies, produces dist/main.js and dist/desktop.js)
  console.log('Building for Node with Bun...')
  await run(bunBin, ['run', 'build:node'])

  const outfile = path.join(root, 'dist', 'desktop-bundled.js')
  console.log(`Building Electron main process to ${outfile}...`)
  
  await run(bunBin, [
    'build', 'scripts/electron-main.js',
    '--outfile', outfile,
    '--target', 'node',
    '--external', 'electron',
    '--format', 'esm',
    '--minify', '--bundle'
  ])

  // Verify and fix location if Bun misbehaved
  if (!fs.existsSync(outfile)) {
    const wrongLoc = path.join(root, 'scripts', 'desktop-bundled.js')
    if (fs.existsSync(wrongLoc)) {
      console.log(`Moving build output from ${wrongLoc} to ${outfile}...`)
      fs.renameSync(wrongLoc, outfile)
      if (fs.existsSync(wrongLoc + '.map')) {
        fs.renameSync(wrongLoc + '.map', outfile + '.map')
      }
    }
  }

  const electronBuilderPath = path.join(root, 'node_modules', '.bin', 'electron-builder')
  const env = { ...process.env }

  const platform = process.platform
  const args = [electronBuilderPath]

  // Add build arguments
  if (process.argv.includes('--all')) {
    args.push('-mwl')
  } else if (platform === 'linux') {
    args.push('--linux')
  } else if (platform === 'darwin') {
    args.push('--mac')
  } else if (platform === 'win32') {
    args.push('--win')
  }

  console.log('Building Electron app...')
  await run(bunBin, args, { env })
}

main().catch((e) => {
  console.error(e)
  process.exit(1)
})