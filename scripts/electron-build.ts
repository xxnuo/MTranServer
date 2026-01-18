import assert from 'node:assert/strict'
import { execFileSync, spawn } from 'child_process'
import { createRequire } from 'node:module'
import path from 'path'
import fs from 'fs'
import { installBundle, smokeScript, verifyBundle } from './prepare-ort-macos-x64'

const platformFlags = { linux: '--linux', darwin: '--mac', win32: '--win' } as const
type Platform = keyof typeof platformFlags

export function getBuildTargets(argv: string[], platform: string, arch: string) {
  const options = new Set(argv.filter(arg => arg !== '--'))
  for (const arg of options) {
    assert(['--all', '--linux', '--mac', '--win', '--x64', '--arm64'].includes(arg), `Unknown build argument: ${arg}`)
  }
  assert(Object.hasOwn(platformFlags, platform), `Unsupported host platform: ${platform}`)
  assert(['x64', 'arm64'].includes(arch), `Unsupported Node architecture: ${arch}`)
  const requested = (Object.keys(platformFlags) as Platform[]).filter(p => options.has(platformFlags[p]))
  const platforms = options.has('--all') ? Object.keys(platformFlags) as Platform[] : requested.length ? requested : [platform as Platform]
  const explicitArchs = ['x64', 'arm64'].filter(a => options.has(`--${a}`))
  const archs = explicitArchs.length ? explicitArchs : options.has('--all') ? ['x64', 'arm64'] : [arch]
  return { platforms, archs }
}

function run(command: string, args: string[], options?: { cwd?: string; env?: NodeJS.ProcessEnv }) {
  return new Promise<void>((resolve, reject) => {
    const proc = spawn(command, args, {
      stdio: 'inherit',
      env: options?.env ?? process.env,
      cwd: options?.cwd
    })
    proc.on('error', reject)
    proc.on('exit', (code) => {
      if (code === 0) resolve()
      else reject(new Error(`exit ${code}`))
    })
  })
}

async function main() {
  const bunBin = process.execPath
  const root = process.cwd()
  const require = createRequire(path.join(root, 'package.json'))
  const pkg = require('./package.json')
  // Bun can run as x64 on an ARM64 host; native Node is the packaging authority.
  const node = JSON.parse(execFileSync('node', ['-p', 'JSON.stringify({platform:process.platform,arch:process.arch})'], { encoding: 'utf8' }))
  const { platforms, archs } = getBuildTargets(process.argv.slice(2), node.platform, node.arch)
  const targets = platforms.flatMap(platform => archs.map(arch => `${platform}-${arch}`))
  const crossTargets = targets.filter(target => target !== `${node.platform}-${node.arch}`)
  assert(!crossTargets.length || process.env.MTRAN_NATIVE_ARTIFACT_DIR,
    `Cross-target builds (including --all) require verified target addons and Intel ORT in MTRAN_NATIVE_ARTIFACT_DIR: ${crossTargets.join(', ')}`)

  // Build for Node (bundles dependencies, produces dist/main.js and dist/desktop.js)
  console.log('Building for Node with Bun...')
  await run(bunBin, ['run', 'build:node'])

  for (const target of targets) {
    const addon = path.join(root, 'dist', `onnx-async-${target}.node`)
    assert(fs.existsSync(addon) && fs.statSync(addon).size > 0, `Missing target native SSRU addon: ${addon}`)
  }
  if (targets.includes('darwin-x64')) {
    const destination = path.join(path.dirname(require.resolve('onnxruntime-node/package.json')), 'bin/napi-v6/darwin/x64')
    installBundle(path.join(root, 'dist/ort/darwin/x64'), destination, () => { verifyBundle(destination) })
  }
  if (targets.includes(`${node.platform}-${node.arch}`)) {
    console.log('Checking native addon and ORT in the actual Electron host (no GUI)...')
    const smoke = `
      import { createRequire as createHostRequire } from 'node:module';
      const hostRequire = createHostRequire(process.argv[1]);
      hostRequire('node:assert/strict').equal(process.versions.electron, ${JSON.stringify(pkg.build.electronVersion)}, 'Smoke runtime must match packaged Electron');
      hostRequire('node:assert/strict').equal(process.platform + '-' + process.arch, ${JSON.stringify(`${node.platform}-${node.arch}`)}, 'Electron architecture mismatch');
      const addon = hostRequire(${JSON.stringify(path.join(root, 'dist', `onnx-async-${node.platform}-${node.arch}.node`))});
      hostRequire('node:assert/strict').equal(addon.ssruVersion, 1, 'Native SSRU unavailable in Electron');
      for (const name of ['init', 'run', 'close']) hostRequire('node:assert/strict').equal(typeof addon[name], 'function');
      ${smokeScript}
      const fs = require('node:fs'), path = require('node:path');
      fs.mkdirSync('.tmp', {recursive:true});
      const directory = fs.mkdtempSync(path.resolve('.tmp/electron-smoke-'));
      try {
        const filename = path.join(directory, 'smoke.onnx');
        // ONNX int64[2]: Y = X + eos_id, exercises the addon's async N-API boundary.
        fs.writeFileSync(filename, Buffer.from('CAg6XwoTCgFYCgZlb3NfaWQSAVkiA0FkZBIOZWxlY3Ryb25fc21va2VaEAoBWBILCgkIBxIFCgMSAW5aFAoGZW9zX2lkEgoKCAgHEgQKAggBYhAKAVkSCwoJCAcSBQoDEgFuQgQKABAN', 'base64'));
        const library = path.resolve(path.dirname(require.resolve('onnxruntime-node')), '../bin/napi-v6', process.platform, process.arch,
          process.platform === 'win32' ? 'onnxruntime.dll' : process.platform === 'darwin' ? 'libonnxruntime.1.dylib' : 'libonnxruntime.so.1');
        const handle = await addon.init(filename, library, 1);
        try { assert.deepEqual(await addon.run(handle, BigInt64Array.of(2n, 0n), null, 7n), BigInt64Array.of(9n, 7n)); }
        finally { await addon.close(handle); }
      } finally { fs.rmSync(directory, {recursive:true, force:true}); }
      console.log('Electron native addon init/run/close and ORT inference passed');
    `
    await run(require('electron'), ['--input-type=module', '-e', smoke, path.join(root, 'package.json')], {
      env: { ...process.env, ELECTRON_RUN_AS_NODE: '1' }
    })
  }

  const outfile = path.join(root, 'dist', 'desktop-bundled.js')
  console.log(`Building Electron main process to ${outfile}...`)
  
  await run(bunBin, [
    'build', 'scripts/electron-main.js',
    '--outfile', outfile,
    '--target', 'node',
    '--external', 'electron',
    '--external', 'onnxruntime-node',
    '--external', '@sctg/sentencepiece-js',
    '--format', 'esm',
    '--banner', "import './bootstrap-runtime.js';",
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

  const args = [electronBuilderPath]

  // Explicit target names bypass package.json's two-architecture target defaults.
  for (const platform of platforms) {
    const config = pkg.build[platform === 'darwin' ? 'mac' : platform === 'win32' ? 'win' : 'linux'].target
    const names = (Array.isArray(config) ? config : [config]).map(target => typeof target === 'string' ? target : target.target)
    args.push(platformFlags[platform], ...names)
  }
  args.push(...archs.map(arch => `--${arch}`))

  console.log('Building Electron app...')
  await run(bunBin, args, { env })
}

if (import.meta.main) main().catch((e) => {
  console.error(e)
  process.exit(1)
})
