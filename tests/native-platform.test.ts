import assert from 'node:assert/strict';
import { test } from 'bun:test';
import { copyFileSync, cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import protobuf from 'protobufjs';
import schema from '../src/converter/schema.json';
import { buildNative } from '../scripts/build-native';

const root = path.resolve(import.meta.dir, '..');
const platforms = ['win32', 'linux', 'darwin'] as const;
const architectures = ['x64', 'arm64'] as const;
const transpiler = new Bun.Transpiler({ loader: 'ts' });

// Exercise build decisions without pretending mocked platforms ran native code.
function simulatedBuild(platform: string, arch: string, options: {
  compiler?: string; cached?: boolean; missing?: 'headers' | 'library'; packedFails?: boolean; vnniFails?: boolean; linkFails?: boolean;
} = {}) {
  const paths = platform === 'win32' ? path.win32 : path.posix;
  const directory = platform === 'win32' ? 'C:\\native 空格' : '/native 空格';
  const include = paths.join(directory, 'include/node'), library = paths.join(directory, arch, 'node.lib');
  const env = options.cached ? { LOCALAPPDATA: directory } : { NODE_INCLUDE_DIR: include, NODE_LIB_FILE: library };
  const calls: { command: string; args: string[]; options: any }[] = [], removed: string[] = [], warnings: string[] = [];
  const source = readFileSync(path.join(root, 'scripts/build-native.ts'), 'utf8')
    .replace(/^import .*;\r?\n/gm, '')
    .replace("const root = fileURLToPath(new URL('..', import.meta.url));", 'const root = taskRoot;')
    .replace('export function buildNative', 'function buildNative')
    .replace('if (import.meta.main) buildNative();', 'return buildNative();');
  const output = new Function('taskRoot', 'process', 'path', 'homedir', 'spawnSync', 'existsSync', 'mkdirSync',
    'mkdtempSync', 'renameSync', 'rmSync', 'console', transpiler.transformSync(source))(
    directory, { platform, arch: 'x64', env: { ...env, CXX: options.compiler }, pid: 123, versions: { node: '99.0.0' }, execPath: paths.join(directory, 'bin/bun') },
    paths, () => directory,
    (command: string, args: string[], settings: any) => {
      calls.push({ command, args, options: settings });
      const packed = args.some(arg => arg.endsWith('packed.cc'));
      const vnni = args.some(arg => arg.endsWith('packed_vnni.cc'));
      const link = args.some(arg => arg.endsWith('addon.cc'));
      return { status: packed && options.packedFails || vnni && options.vnniFails || link && options.linkFails ? 1 : 0,
        stdout: command === 'node' ? `22.23.5 ${arch}\n` : '', stderr: 'compiler diagnostic' };
    },
    (file: string) => {
      if (file.endsWith('node_api.h')) return options.missing !== 'headers' &&
        (options.cached ? file.includes(paths.join('node-gyp/Cache', '22.23.5')) : file === paths.join(include, 'node_api.h'));
      return options.missing !== 'library' && file.endsWith(paths.join(arch, 'node.lib'));
    },
    () => {}, (prefix: string) => prefix + '123', () => {},
    (file: string) => removed.push(file), { log() {}, warn(message: string) { warnings.push(message); } },
  );
  return { output, calls, removed, warnings, library, paths };
}

for (const platform of platforms) for (const arch of architectures) {
  test(`native build arguments: ${platform}/${arch}`, () => {
    for (const compiler of platform === 'win32' ? ['cl.exe', 'clang-cl.exe'] : ['c++']) {
      const result = simulatedBuild(platform, arch, { compiler });
      assert(result.output?.endsWith(`onnx-async-${platform}-${arch}.node`));
      const compile = result.calls.find(call => call.args.some(arg => arg.endsWith('packed.cc')))!;
      const link = result.calls.find(call => call.args.some(arg => arg.endsWith('addon.cc')))!;
      assert(compile && link);
      for (const call of [compile, link])
        assert(!call.args.some(arg => /^-m(avx|arch|cpu)|^\/arch:/i.test(arg)), 'global ISA flags would break older CPUs');
      assert(link.args.includes(platform === 'win32' ? '/DMTRAN_SSRU_PACKED' : '-DMTRAN_SSRU_PACKED'));
      const vnni = result.calls.find(call => call.args.some(arg => arg.endsWith('packed_vnni.cc')));
      const hasVnni = arch === 'x64' && (platform !== 'win32' || compiler === 'clang-cl.exe');
      assert.equal(!!vnni, hasVnni);
      assert.equal(compile.args.some(arg => arg.includes('MTRAN_SSRU_HAVE_VNNI')), hasVnni);
      assert.equal(link.args.some(arg => /packed-vnni\.(o|obj)$/.test(arg)), hasVnni);
      if (vnni) {
        assert(vnni.args.includes(platform === 'win32' ? '/clang:-mavx2' : '-mavx2'));
        assert(vnni.args.includes(platform === 'win32' ? '/clang:-mavxvnni' : '-mavxvnni'));
      }
      if (platform === 'win32') {
        assert(link.args.includes(result.library));
        assert(link.args.includes(`/MACHINE:${arch === 'x64' ? 'X64' : 'ARM64'}`));
        assert(link.args.includes('/DELAYLOAD:node.exe'));
        assert(link.args.includes('delayimp.lib'));
        assert(link.args.includes('/DBUILDING_NODE_EXTENSION'));
        assert(link.args.includes('/DNOMINMAX'));
        assert(link.args.some(arg => arg.startsWith('/OUT:') && arg.endsWith('.build-123')));
        assert(link.options.env.TEMP && link.options.env.TMP);
        if (compiler === 'clang-cl.exe') assert(compile.args.includes(`--target=${arch === 'x64' ? 'x86_64' : 'aarch64'}-pc-windows-msvc`));
        else assert(result.calls.some(call => call.command === compiler && call.args[0] === '/?'));
      } else {
        assert(link.args.includes(platform === 'darwin' ? 'dynamic_lookup' : '-ldl'));
        if (platform === 'darwin') for (const call of [compile, link])
          assert.equal(call.args[call.args.indexOf('-arch') + 1], arch === 'arm64' ? 'arm64' : 'x86_64');
      }
    }
  });
}

test('Windows reuses the actual Node version cache and diagnoses missing build prerequisites', () => {
  assert(simulatedBuild('win32', 'arm64', { cached: true }).output);
  for (const missing of ['headers', 'library'] as const) {
    const result = simulatedBuild('win32', 'x64', { missing });
    assert.equal(result.output, null);
    assert(result.warnings.join('\n').includes(missing === 'headers' ? 'NODE_INCLUDE_DIR' : 'NODE_LIB_FILE'));
    assert(!result.calls.some(call => call.args.some(arg => arg.endsWith('addon.cc'))));
  }
});

test('packed compile failures retain InvokeOp and link failures remove stale addons', () => {
  for (const platform of platforms) {
    const result = simulatedBuild(platform, 'x64', { packedFails: true });
    assert(result.output);
    const link = result.calls.find(call => call.args.some(arg => arg.endsWith('addon.cc')))!;
    assert(!link.args.some(arg => arg.includes('MTRAN_SSRU_PACKED') || /packed(?:-vnni)?\.(o|obj)$/.test(arg)));
    assert(result.warnings.join('\n').includes('retaining InvokeOp'));
    const failed = simulatedBuild(platform, 'x64', { linkFails: true });
    assert.equal(failed.output, null);
    assert(failed.removed.some(file => file.endsWith(`onnx-async-${platform}-x64.node`)));
    assert(failed.warnings.join('\n').includes('compiler diagnostic'));
  }
  assert.equal(simulatedBuild('freebsd', 'x64').output, null);
  assert.equal(simulatedBuild('win32', 'ia32').output, null);
});

test('optional VNNI failure keeps packed AVX2 and never leaves an unresolved VNNI symbol', () => {
  for (const [platform, compiler] of [['linux', 'c++'], ['darwin', 'clang++'], ['win32', 'clang-cl.exe']]) {
    const result = simulatedBuild(platform, 'x64', { compiler, vnniFails: true });
    assert(result.output);
    const compile = result.calls.find(call => call.args.some(arg => arg.endsWith('packed.cc')))!;
    const link = result.calls.find(call => call.args.some(arg => arg.endsWith('addon.cc')))!;
    assert(!compile.args.some(arg => arg.includes('MTRAN_SSRU_HAVE_VNNI')));
    assert(!link.args.some(arg => /packed-vnni\.(o|obj)$/.test(arg)));
    assert(link.args.some(arg => arg.includes('MTRAN_SSRU_PACKED')));
    assert(result.warnings.some(message => message.includes('retaining exact AVX2')));
    assert(!result.removed.some(file => file.endsWith('.node')));
  }
});

test('native wrapper resolves all six ORT library paths and falls back after load errors', async () => {
  const source = readFileSync(path.join(root, 'src/core/native.ts'), 'utf8').replace(/^import .*;\r?\n/gm, '')
    .replaceAll('import.meta.url', JSON.stringify(pathToFileURL(path.join(root, 'src/core/native.ts')).href))
    .replace(/^export /gm, '');
  const make = new Function('process', 'ort', 'existsSync', 'createRequire', 'fileURLToPath', 'path', 'logger',
    transpiler.transformSync(source) + '\nreturn { hasNativeSsru, createNativeSession };');
  for (const platform of platforms) for (const arch of architectures) for (const fails of [false, true]) {
    const paths = platform === 'win32' ? path.win32 : path.posix;
    const packageEntry = platform === 'win32' ? 'C:\\node modules\\onnxruntime-node\\dist\\index.js' : '/node modules/onnxruntime-node/dist/index.js';
    let loadedLibrary = '', closed = 0;
    const binding = { ssruVersion: 1, async init(_model: string, library: string) {
      loadedLibrary = library;
      if (fails) throw new Error('library unavailable');
      return {};
    }, async close() { closed++; } };
    const require = Object.assign(() => binding, { resolve: () => packageEntry });
    const native = make({ platform, arch }, {}, () => true, () => require, fileURLToPath, paths, { warn() {} });
    assert(native.hasNativeSsru());
    const session = await native.createNativeSession('模型.onnx', ['X', 'eos_id'], 1, true);
    const library = platform === 'win32' ? 'onnxruntime.dll' : platform === 'darwin' ? 'libonnxruntime.1.dylib' : 'libonnxruntime.so.1';
    assert.equal(loadedLibrary, paths.resolve(paths.dirname(packageEntry), '../bin/napi-v6', platform, arch, library));
    assert.equal(!!session, !fails);
    if (session) { await session.release(); await session.release(); assert.equal(closed, 1); }
  }
  for (const [platform, arch] of [['freebsd', 'x64'], ['win32', 'ia32']]) {
    const native = make({ platform, arch }, {}, () => { throw new Error('unsupported platform loaded addon'); }, null, fileURLToPath, path, {});
    assert.equal(native.hasNativeSsru(), false);
  }
});

const modelType = protobuf.Root.fromJSON(schema).lookupType('onnx.ModelProto');
const value = (name: string, length: number | string) => ({ name, type: { tensorType: { elemType: 7,
  shape: { dim: [typeof length === 'number' ? { dimValue: length } : { dimParam: length }] } } } });

test.skipIf(!platforms.includes(process.platform as any) || !architectures.includes(process.arch as any))(
  'host production addon: Unicode library/model paths, ORT runs, SSRU, validation and draining close', () => {
    const binary = buildNative();
    assert(binary, 'native platform acceptance requires a compiler, NODE_INCLUDE_DIR and Windows NODE_LIB_FILE');
    const node = spawnSync('node', ['-p', 'process.arch'], { encoding: 'utf8' });
    assert.equal(node.status, 0, 'native acceptance must run on Node');
    const arch = node.stdout.trim();
    assert(binary.endsWith(`-${arch}.node`), 'addon must target the actual Node architecture');
    mkdirSync(path.join(root, '.tmp'), { recursive: true });
    const directory = mkdtempSync(path.join(root, '.tmp/native-platform-'));
    try {
      const unicode = path.join(directory, '原生 空格-𠀀'); mkdirSync(unicode);
      const libraryName = process.platform === 'win32' ? 'onnxruntime.dll'
        : process.platform === 'darwin' ? 'libonnxruntime.1.dylib' : 'libonnxruntime.so.1';
      const ortDirectory = path.join(path.dirname(require.resolve('onnxruntime-node/package.json')), 'bin/napi-v6', process.platform, arch);
      assert(existsSync(path.join(ortDirectory, libraryName)), 'ORT 1.30.0 must be supplied for this host; no version substitution');
      const libraryDirectory = path.join(unicode, '运行库'); mkdirSync(libraryDirectory);
      const library = path.join(libraryDirectory, libraryName);
      if (process.platform === 'win32') cpSync(ortDirectory, libraryDirectory, { recursive: true });
      else copyFileSync(path.join(ortDirectory, libraryName), library);
      const save = (name: string, graph: object, custom = false) => {
        const file = path.join(unicode, name + '.onnx');
        writeFileSync(file, modelType.encode(modelType.create({ irVersion: 8,
          opsetImport: [{ domain: '', version: 13 }, ...(custom ? [{ domain: 'com.mtran', version: 1 }] : [])], graph })).finish());
        return file;
      };
      const models = [false, true].map(shortlist => save(shortlist ? '短列表' : '标准', { name: 'platform',
        input: [value('X', 'n'), value('eos_id', 1), ...(shortlist ? [value('candidate_ids', 'c')] : [])], output: [value('Y', 'n')],
        node: shortlist ? [{ opType: 'Gather', input: ['candidate_ids', 'X'], output: ['picked'] },
          { opType: 'Add', input: ['picked', 'eos_id'], output: ['Y'] }] : [{ opType: 'Add', input: ['X', 'eos_id'], output: ['Y'] }],
      }));
      const initializers: any[] = [];
      const add = (name: string, dims: number[], fill = 0) => initializers.push({ name, dims, dataType: 1,
        rawData: Buffer.from(new Float32Array(dims.reduce((a, b) => a * b, 1)).fill(fill).buffer) });
      add('Wemb', [12, 4]); add('decoder_ff_logit_out_b', [1, 12]);
      for (const name of ['rnn_W', 'rnn_Wf', 'context_Wq', 'context_Wo', 'ffn_W1']) add('decoder_l1_' + name, [4, 4]);
      for (const name of ['rnn_bf', 'context_bq', 'context_bo', 'ffn_b1']) add('decoder_l1_' + name, [1, 4]);
      for (const name of ['rnn_ffn_ln', 'context_Wo_ln', 'ffn_ffn_ln']) {
        add('decoder_l1_' + name + '_scale', [1, 4], 1); add('decoder_l1_' + name + '_bias', [1, 4]);
      }
      const names = initializers.map(tensor => tensor.name).sort();
      add('K', [1, 1, 4, 2]); add('V', [1, 1, 2, 4]);
      const ssru = save('循环解码', { name: 'ssru-platform', initializer: initializers,
        input: [value('X', 2), value('eos_id', 1), value('candidate_ids', 'c')], output: [value('Y', 'n')],
        node: [{ opType: 'SsruDecode', domain: 'com.mtran', input: ['X', 'eos_id', 'candidate_ids', 'K', 'V', ...names], output: ['Y'],
          attribute: [...Object.entries({ dim: 4, heads: 1, depth: 1, ffn_depth: 1 }).map(([name, i]) => ({ name, type: 2, i })),
            { name: 'weight_names', type: 8, strings: names.map(name => Buffer.from(name)) }] }],
      }, true);
      const script = String.raw`
        const assert = require('node:assert/strict'), addon = require(${JSON.stringify(binary)});
        const models = ${JSON.stringify(models)}, library = ${JSON.stringify(library)};
        (async () => {
          assert.equal(addon.ssruVersion, 1);
          assert(['invokeop', 'packed-avxvnni', 'packed-avx2', 'packed-avx2-ort', 'packed-sdot', 'packed-neon'].includes(addon.ssruBackend));
          await assert.rejects(addon.init(models[0], library + '.missing'));
          await assert.rejects(addon.init(models[0] + '.missing', library));
          await assert.rejects(addon.init(models[0] + '\0suffix', library), /NUL/);
          await assert.rejects(addon.init(models[0], library + '\0suffix'), /NUL/);
          for (let cycle = 0; cycle < 3; cycle++) for (const shortlist of [false, true]) {
            const handle = await addon.init(models[Number(shortlist)], library, 1);
            const expected = shortlist ? BigInt64Array.of(37n, 17n) : BigInt64Array.of(9n, 7n);
            const runs = Array.from({length: 16}, () => addon.run(handle, BigInt64Array.of(2n, 0n),
              shortlist ? BigInt64Array.of(10n, 20n, 30n) : null, 7n));
            const closing = addon.close(handle);
            for (const result of await Promise.all(runs)) assert.deepEqual(result, expected);
            await closing; await addon.close(handle);
            await assert.rejects(addon.run(handle, BigInt64Array.of(0n), null, 0n));
          }
          const handle = await addon.init(${JSON.stringify(ssru)}, library, 1);
          assert.deepEqual(await addon.run(handle, BigInt64Array.of(2n, 0n), BigInt64Array.of(0n, 2n), 0n), BigInt64Array.of(0n));
          await addon.close(handle);
          console.log(JSON.stringify({platform:process.platform, arch:process.arch, backend:addon.ssruBackend, ort:'1.30.0'}));
        })().catch(error => { console.error(error); process.exitCode = 1; });
      `;
      const result = spawnSync('node', ['--eval', script], { cwd: root, encoding: 'utf8', timeout: 60_000 });
      assert.equal(result.status, 0, `${result.error ?? ''}\n${result.stdout}\n${result.stderr}`);
      console.log(result.stdout.trim());
    } finally { rmSync(directory, { recursive: true, force: true }); }
  }, 120_000);
