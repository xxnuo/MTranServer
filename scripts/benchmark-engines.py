"""Linux HTTP benchmark: run one engine at a time, with isolated model caches."""
import argparse, concurrent.futures, hashlib, json, os, pathlib, shutil, socket, statistics, subprocess, time, urllib.request

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--models', type=pathlib.Path, required=True)
parser.add_argument('--records', type=pathlib.Path, required=True)
parser.add_argument('--linguaspark', type=pathlib.Path, required=True)
parser.add_argument('--linguaspark-revision', required=True)
parser.add_argument('--work-dir', type=pathlib.Path, required=True)
parser.add_argument('--output', type=pathlib.Path, required=True)
parser.add_argument('--cpus', default=','.join(map(str, sorted(os.sched_getaffinity(0)))))
args = parser.parse_args()
root = pathlib.Path.cwd()
work = args.work_dir.resolve()
assert work.is_relative_to(root / '.tmp') and not work.exists(), 'Use a new directory inside .tmp'
assert not args.output.exists(), 'Output already exists'
cpus = [int(x) for x in args.cpus.split(',')]
assert cpus and set(cpus).issubset(os.sched_getaffinity(0)), 'Unavailable CPUs'
assert set(cpus) == set(range(os.cpu_count())), 'Use all host CPUs: ORT automatic threads can override narrower affinity masks'
corpus_path = root / 'docs/benchmarks/fast-corpus.json'
corpus = json.loads(corpus_path.read_text())
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
work.mkdir(parents=True)
models = {}
for pair in ['en_zh-Hans', 'zh-Hans_en']:
    folder = args.models / pair
    manifest = json.loads((folder / 'manifest.json').read_text())
    models[pair] = {x['role']: sha(folder / x['path']) for x in manifest['files'] if x['role'].startswith('source-')}
for name in ['original', 'fast']:
    for pair in models:
        shutil.copytree(args.models / pair, work / name / 'models' / pair)
    (work / name / 'config').mkdir()
    shutil.copyfile(args.records, work / name / 'config/records.json')
(work / 'linguaspark-models').mkdir()
for source, target in [('en_zh-Hans', 'en-zh'), ('zh-Hans_en', 'zh-en')]:
    (work / 'linguaspark-models' / target).symlink_to((args.models / source).resolve(), target_is_directory=True)

def request(url, payload=None):
    req = urllib.request.Request(url, data=None if payload is None else json.dumps(payload).encode(), headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=180) as response:
        return json.load(response)

def memory(pid):
    fields = dict(line.split(':', 1) for line in pathlib.Path(f'/proc/{pid}/status').read_text().splitlines() if ':' in line)
    return {'rss_mib': int(fields['VmRSS'].split()[0]) / 1024, 'peak_mib': int(fields['VmHWM'].split()[0]) / 1024}

results = []
outputs = {}
runtime_selections = []
# Rotate engine order; no idle ONNX spinning pools overlap another engine's timing.
for round in range(3):
    order = ['original', 'fast', 'linguaspark']
    order = order[round:] + order[:round]
    for name in order:
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
        url = f'http://127.0.0.1:{port}'
        env = {**os.environ, 'MT_CPU_ONLY': 'true', 'MT_CACHE_SIZE': '0', 'MT_FAST': 'false', 'MT_LEXICAL_SHORTLIST': 'false'}
        if name == 'linguaspark':
            command = [str(args.linguaspark.resolve())]
            env.update(MODELS_DIR=str(work / 'linguaspark-models'), PORT=str(port), IP='127.0.0.1', NUM_WORKERS=str(len(cpus)), RUST_LOG='warn')
        else:
            directory = work / name
            command = ['node', str(root / 'dist/main.js'), '--host', '127.0.0.1', '--port', str(port), '--cache-size', '0', '--workers-per-language', '0', '--offline', '--no-ui', '--no-check-update', '--log-level', 'error', '--fullwidth-zh-punctuation=false', '--model-dir', str(directory / 'models'), '--config-dir', str(directory / 'config'), '--log-dir', str(directory / 'logs'), '--fast' if name == 'fast' else '--no-fast']
        with (work / f'{round}-{name}.log').open('w') as log:
            proc = subprocess.Popen(['taskset', '-c', args.cpus, *command], env=env, stdout=log, stderr=log)
            try:
                for attempt in range(300):
                    if proc.poll() is not None:
                        raise RuntimeError(f'{name} exited: see {log.name}')
                    try:
                        ready = request(url + ('/health' if name == 'linguaspark' else '/ui/api/settings'))
                        break
                    except Exception:
                        time.sleep(.1)
                else:
                    raise RuntimeError(f'{name} readiness timeout')
                if name != 'linguaspark':
                    assert ready['config']['server']['cacheSize'] == 0
                    assert ready['config']['server']['fast'] == (name == 'fast')
                def translate(index):
                    row = corpus[index]
                    start = time.perf_counter()
                    result = request(url + '/translate', {k: row[k] for k in ['from', 'to', 'text']})
                    elapsed = (time.perf_counter() - start) * 1000
                    text = result['text' if name == 'linguaspark' else 'result']
                    assert isinstance(text, str) and text, (name, index, result)
                    return elapsed, text
                for index in range(len(corpus)):
                    _, text = translate(index)
                    key = (name, index)
                    if key in outputs:
                        assert outputs[key] == text, f'Non-deterministic warmup: {key}'
                    outputs[key] = text
                groups = sorted({row['group'] for row in corpus})
                for group in groups:
                    group_indices = [i for i, row in enumerate(corpus) if row['group'] == group]
                    for concurrency in ([1, 8] if group.startswith('SHORT') else [1]):
                        indices = group_indices * (24 if concurrency == 8 else 3)
                        start = time.perf_counter()
                        with concurrent.futures.ThreadPoolExecutor(concurrency) as pool:
                            measured = list(pool.map(translate, indices))
                        wall = time.perf_counter() - start
                        assert all(text == outputs[(name, index)] for index, (_, text) in zip(indices, measured)), 'Non-deterministic measured output'
                        results.append({'round': round, 'engine': name, 'group': group, 'concurrency': concurrency, 'requests': len(indices), 'wall_ms': wall * 1000, 'requests_per_second': len(indices) / wall, 'latencies_ms': [ms for ms, _ in measured]})
                if name != 'linguaspark':
                    runtimes = request(url + '/ui/api/settings')['engines']
                    assert all(engine.get('runtime') == 'native-async' for engine in runtimes), runtimes
                    runtime_selections.append({'round': round, 'engine': name, 'runtimes': [{key: engine.get(key) for key in ['from', 'to', 'provider', 'runtime', 'cpuThreads', 'maxConcurrentRuns', 'tuning']} for engine in runtimes]})
                print(round, name, 'PASS', flush=True)
                for row in results:
                    if row['round'] == round and row['engine'] == name:
                        row['memory_after_all_scenarios'] = memory(proc.pid)
            finally:
                proc.terminate()
                try: proc.wait(timeout=15)
                except subprocess.TimeoutExpired: proc.kill(); proc.wait()

summary = []
for name in ['original', 'fast', 'linguaspark']:
    for group, concurrency in sorted({(x['group'], x['concurrency']) for x in results}):
        rows = [x for x in results if x['engine'] == name and x['group'] == group and x['concurrency'] == concurrency]
        assert len(rows) == 3
        summary.append({'engine': name, 'group': group, 'concurrency': concurrency, 'mean_latency_ms': statistics.median(statistics.mean(x['latencies_ms']) for x in rows), 'requests_per_second': statistics.median(x['requests_per_second'] for x in rows), 'requests_total': sum(x['requests'] for x in rows)})
cpu = next(line.split(':', 1)[1].strip() for line in pathlib.Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name'))
report = {'finished_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'platform': 'Linux x64', 'cpu': cpu, 'allowed_cpus': cpus, 'cache_size': 0, 'rounds': 3, 'linguaspark_workers': len(cpus), 'linguaspark_revision': args.linguaspark_revision, 'linguaspark_binary_sha256': sha(args.linguaspark.resolve()), 'mtran_main_sha256': sha(root / 'dist/main.js'), 'native_addon_sha256': sha(root / 'dist/onnx-async-linux-x64.node'), 'uv_threadpool_size': os.environ.get('UV_THREADPOOL_SIZE', '4'), 'mtran_worker_sha256': sha(root / 'dist/converter-worker.js'), 'corpus_sha256': sha(corpus_path), 'runtime_selections': runtime_selections, 'node_version': subprocess.check_output(['node', '--version'], text=True).strip(), 'onnxruntime_version': json.loads((root / 'node_modules/onnxruntime-node/package.json').read_text())['version'], 'models': models, 'summary': summary, 'results': results, 'translations': [{'engine': name, 'corpus_index': index, 'text': text} for (name, index), text in sorted(outputs.items())]}
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
print(args.output, flush=True)
