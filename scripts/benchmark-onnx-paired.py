# Run with uv run python scripts/benchmark-onnx-paired.py --help
import argparse, atexit, concurrent.futures, hashlib, http.client, json, math, os, shutil, socket, statistics, subprocess, tempfile, threading, time, urllib.request
from pathlib import Path
parser = argparse.ArgumentParser(description='Compare uncached ONNX server builds on identical prepared models and rotating HTTP workloads.')
parser.add_argument('--models', required=True)
parser.add_argument('--output', required=True)
parser.add_argument('--variant', action='append', required=True, metavar='NAME=SERVER_ENTRY')
args = parser.parse_args()
entries = [(name, Path(entry).resolve()) for name, entry in (value.split('=', 1) for value in args.variant)]
assert len(entries) >= 2 and len({name for name, _ in entries}) == len(entries)
assert all(name and all(c.isalnum() or c in '-_' for c in name) for name, _ in entries)
assert all((entry.is_file() for _, entry in entries))
model_root = Path(args.models).resolve()
output = Path(args.output).resolve()
output.parent.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='mtran-onnx-paired-'))
atexit.register(shutil.rmtree, root, ignore_errors=True)
corpus_path = Path(__file__).resolve().parents[1] / 'benchmarks/onnx-corpus.json'
reservations = []
ports = []
for _ in entries:
    sock = socket.socket()
    sock.bind(('127.0.0.1', 0))
    ports.append(sock.getsockname()[1])
    reservations.append(sock)
for sock in reservations:
    sock.close()
corpus = json.loads(corpus_path.read_text())
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
report = {'testedAt': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'method': 'Identical prepared qall models, Python keep-alive loopback HTTP client, cache=0, eight warmups, three rotating rounds per workload, with a 250 ms quiet interval before each timed block. Model loading and automatic tuning excluded. Same inputs and punctuation settings for every variant.', 'corpusSha256': sha(corpus_path), 'loadAtStart': os.getloadavg(), 'results': []}
for from_, to in [('en', 'zh-Hans'), ('zh-Hans', 'en'), ('de', 'en')]:
    children = []
    handles = []
    variants = {}
    pair = f'{from_}_{to}'
    try:
        for (name, entry), port in zip(entries, ports):
            work = root / 'diagnostic' / f'paired-{name}-{pair}'
            work.mkdir(parents=True, exist_ok=True)
            if not (work / 'models' / pair).exists():
                shutil.copytree(model_root / pair, work / 'models' / pair)
            (work / 'config').mkdir(exist_ok=True)
            manifest = json.loads((work / 'models' / pair / 'manifest.json').read_text())
            (work / 'config/records.json').write_text(json.dumps({'data': manifest['records']}))
            args = ['node', str(entry), '--host', '127.0.0.1', '--port', str(port), '--config-dir', str(work / 'config'), '--model-dir', str(work / 'models'), '--log-dir', str(work / 'logs'), '--offline', '--cache-size', '0', '--worker-idle-timeout', '600', '--api-token', '', '--no-check-update', '--no-log-console', '--fullwidth-zh-punctuation']
            log = (work / 'server.log').open('w')
            handles.append(log)
            child = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
            children.append(child)
            url = f'http://127.0.0.1:{port}'
            for _ in range(200):
                assert child.poll() is None, f'{name} exited'
                try:
                    with urllib.request.urlopen(url + '/health', timeout=1) as response:
                        if response.status == 200:
                            break
                except OSError:
                    time.sleep(0.1)
            else:
                raise RuntimeError('readiness timeout')
            variants[name] = {'url': url, 'entrySha256': sha(entry), 'modelSha256': sha(work / 'models' / pair / 'model.onnx'), 'cases': []}

        client = threading.local()

        def request(variant, text):
            started = time.perf_counter()
            if getattr(client, 'url', None) != variant['url']:
                if hasattr(client, 'connection'):
                    client.connection.close()
                client.connection = http.client.HTTPConnection('127.0.0.1', int(variant['url'].rsplit(':', 1)[1]), timeout=180)
                client.url = variant['url']
            data = json.dumps({'from': from_, 'to': to, 'text': text}).encode()
            client.connection.request('POST', '/translate', body=data, headers={'Content-Type': 'application/json'})
            with client.connection.getresponse() as response:
                assert response.status == 200, response.read().decode()
                result = json.loads(response.read())['result']
            assert result.strip() and result != text
            return {'elapsedMs': (time.perf_counter() - started) * 1000, 'translation': result}
        sentences = [row[from_] for row in corpus]
        paragraphs = [' '.join(sentences[i:i + 4]) for i in range(0, 32, 4)]
        for name, variant in variants.items():
            for text in sentences[:8]:
                request(variant, text)
            with urllib.request.urlopen(variant['url'] + '/ui/api/settings') as response:
                settings = json.load(response)
            assert settings['config']['server']['cacheSize'] == 0
            variant['version'] = settings['version']
            variant['runtime'] = settings.get('engines', [])
        client.connection.close()
        assert len({variant['modelSha256'] for variant in variants.values()}) == 1
        names = list(variants)
        for case, concurrency, texts in [('sentence', 1, sentences), ('sentence', 4, sentences), ('paragraph', 1, paragraphs)]:
            rounds = {name: [] for name in names}
            for round_ in range(3):
                for name in names[round_:] + names[:round_]:
                    time.sleep(0.25)
                    variant = variants[name]
                    started = time.perf_counter()
                    with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as pool:
                        samples = list(pool.map(lambda text: request(variant, text), texts))
                    elapsed = (time.perf_counter() - started) * 1000
                    rounds[name].append({'round': round_ + 1, 'elapsedMs': elapsed, 'requestsPerSecond': len(texts) * 1000 / elapsed, 'loadAverage': os.getloadavg(), 'samples': samples})
            expected = [s['translation'] for s in rounds[names[0]][0]['samples']]
            for name in names:
                all_times = sorted((s['elapsedMs'] for r in rounds[name] for s in r['samples']))
                for r in rounds[name]:
                    assert [s['translation'] for s in r['samples']] == expected, f'translation mismatch {pair}/{name}/{case}'
                summary = {'medianRequestsPerSecond': statistics.median((r['requestsPerSecond'] for r in rounds[name])), 'meanMs': statistics.mean(all_times), 'p95Ms': all_times[math.ceil(len(all_times) * 0.95) - 1], 'requests': len(all_times), 'textExact': len(texts)}
                variants[name]['cases'].append({'name': case, 'concurrency': concurrency, 'summary': summary, 'rounds': rounds[name]})
                print(pair, name, case, concurrency, round(summary['medianRequestsPerSecond'], 2), flush=True)
        for name, variant in variants.items():
            with urllib.request.urlopen(variant['url'] + '/ui/api/settings') as response:
                settings = json.load(response)
            assert settings['config']['server']['cacheSize'] == 0
            assert settings.get('engines', []) == variant['runtime'], 'Backend changed during timing'
            assert sha(root / 'diagnostic' / f'paired-{name}-{pair}' / 'models' / pair / 'model.onnx') == variant['modelSha256'], 'Model changed during timing'
        report['results'].append({'pair': pair, 'variants': variants})
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    finally:
        for child in children:
            child.terminate()
            try:
                child.wait(timeout=8)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
        for handle in handles:
            handle.close()
report['loadAtEnd'] = os.getloadavg()
report['finishedAt'] = time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
