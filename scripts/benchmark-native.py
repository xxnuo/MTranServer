import argparse
import concurrent.futures
import datetime
import hashlib
import json
import math
import os
import pathlib
import socket
import statistics
import subprocess
import time
import urllib.request


ROOT = pathlib.Path(__file__).resolve().parents[1]
PAIRS = ("en_zh-Hans", "zh-Hans_en")


def sha(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def request(url, data=None):
    req = urllib.request.Request(
        url, data=None if data is None else json.dumps(data).encode(),
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=180) as response:
        return json.load(response)


def process_stats(pid):
    status = pathlib.Path(f"/proc/{pid}/status").read_text()
    fields = dict(line.split(":", 1) for line in status.splitlines() if ":" in line)
    stat = pathlib.Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    return {
        "cpu_seconds": (int(stat[11]) + int(stat[12])) / os.sysconf("SC_CLK_TCK"),
        "rss_bytes": int(fields["VmRSS"].split()[0]) * 1024,
        "peak_rss_bytes": int(fields["VmHWM"].split()[0]) * 1024,
        "cpus_allowed_list": fields["Cpus_allowed_list"].strip(),
    }


def graphs(model_dir):
    result = {}
    for pair in PAIRS:
        folder = model_dir / pair
        manifest = json.loads((folder / "manifest.json").read_text())
        graph = folder / next(item["path"] for item in manifest["files"] if item["role"] == "inference-model")
        result[pair] = {"path": str(graph.resolve()), "sha256": sha(graph), "identity": manifest["identity"]}
    return result


def main():
    parser = argparse.ArgumentParser(description="ABBA HTTP comparison: ONNX baseline versus native-async; Linux, preinstalled Node and models.")
    parser.add_argument('--standard', action='store_true', help='Compare ordinary mode instead of Fast')
    for name in ("baseline", "candidate", "model-dir", "config-dir", "output"):
        parser.add_argument("--" + name, type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.baseline, args.candidate = args.baseline.resolve(), args.candidate.resolve()
    args.model_dir, args.config_dir, args.output = args.model_dir.resolve(), args.config_dir.resolve(), args.output.absolute()
    if not pathlib.Path("/proc/self/status").is_file():
        parser.error("Linux /proc is required")
    if args.output.exists() or args.output.is_symlink():
        parser.error("output already exists; choose a new JSON file")
    if args.output.suffix != ".json":
        parser.error("--output must name a .json file (for example .tmp/native-benchmark.json)")
    for path in (args.baseline, args.candidate):
        if not path.is_file():
            parser.error(f"bundle not found: {path}")
    for path in (args.model_dir, args.config_dir):
        if not path.is_dir():
            parser.error(f"directory not found: {path}")
    corpus_path = ROOT / "docs/benchmarks/fast-corpus.json"
    corpus = json.loads(corpus_path.read_text())
    groups = [
        ("SHORT_EN", [i for i, row in enumerate(corpus) if row["group"] == "SHORT_EN"]),
        ("SHORT_ZH", [i for i, row in enumerate(corpus) if row["group"] == "SHORT_ZH"]),
        ("MIXED", list(range(len(corpus)))),
    ]
    if not all(indices for _, indices in groups):
        parser.error("corpus must contain both short-text directions")
    probe = r"""
      const {createRequire}=require('node:module'), path=require('node:path'), fs=require('node:fs');
      const req=createRequire(process.argv[1]), ort=req('onnxruntime-node');
      const name='onnx-async-'+process.platform+'-'+process.arch+'.node';
      const candidates=[path.join(path.dirname(process.argv[1]),name),
        path.resolve(path.dirname(process.argv[1]),'../../dist',name)];
      const native=candidates.find(p=>fs.existsSync(p));
      const lib=path.resolve(path.dirname(req.resolve('onnxruntime-node')),'../bin/napi-v6',
        process.platform,process.arch,'libonnxruntime.so.1');
      console.log(JSON.stringify({node:process.version,ort:ort.env.versions,arch:process.arch,native,library:lib}));
    """
    artifacts = {}
    for mode, bundle in (("baseline", args.baseline), ("candidate", args.candidate)):
        info = json.loads(subprocess.check_output(["node", "-e", probe, str(bundle)], text=True, cwd=ROOT))
        info["bundle"] = {"path": str(bundle), "sha256": sha(bundle)}
        info["ort_library_sha256"] = sha(pathlib.Path(info["library"]))
        if info.get("native"):
            info["native_sha256"] = sha(pathlib.Path(info["native"]))
        if mode == "candidate" and not info.get("native"):
            parser.error("candidate native addon not found beside the bundle or in its native fallback directory")
        artifacts[mode] = info
    if artifacts["baseline"]["ort"] != artifacts["candidate"]["ort"]:
        parser.error("baseline and candidate must use the same ORT versions")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    report = {
        "started_at": now(), "completed": False, "fast": not args.standard, "order": ["baseline", "candidate", "candidate", "baseline"],
        "input": {"path": str(corpus_path), "sha256": sha(corpus_path)},
        "script_sha256": sha(pathlib.Path(__file__)), "artifacts": artifacts,
        "model_dir": str(args.model_dir), "config_dir": str(args.config_dir),
        "cpu": {"count": os.cpu_count(), "affinity": sorted(os.sched_getaffinity(0)),
                "cpuinfo": pathlib.Path("/proc/cpuinfo").read_text()},
        "inherited_uv_threadpool_size": os.environ.get("UV_THREADPOOL_SIZE"),
        "records": [],
    }
    expected = {}
    log_dir = args.output.with_name(args.output.name + ".logs")
    with args.output.open("x") as output:
        def save():
            output.seek(0)
            json.dump(report, output, ensure_ascii=False, indent=2)
            output.write("\n")
            output.truncate()
            output.flush()

        save()
        try:
            log_dir.mkdir()
            for block, mode in enumerate(report["order"]):
                bundle = getattr(args, mode)
                with socket.socket() as sock:
                    sock.bind(("127.0.0.1", 0))
                    port = sock.getsockname()[1]
                url = f"http://127.0.0.1:{port}"
                command = [
                    "node", str(bundle), "--host", "127.0.0.1", "--port", str(port),
                    "--no-fast" if args.standard else "--fast", "--workers-per-language", "0", "--cache-size", "0",
                    "--worker-idle-timeout", "3600", "--api-token", "",
                    "--offline", "--no-ui", "--no-check-update", "--log-level", "info",
                    "--fullwidth-zh-punctuation=false", "--model-dir", str(args.model_dir),
                    "--config-dir", str(args.config_dir), "--log-dir", str(log_dir),
                ]
                env = {**os.environ, "MT_CPU_ONLY": "true", "MT_API_TOKEN": "", "MT_LOG_REQUESTS": "false", "MT_LOG_TO_FILE": "false"}
                with (log_dir / f"{block}-{mode}.log").open("x") as log:
                    proc = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=log, env=env)
                    try:
                        for _ in range(300):
                            if proc.poll() is not None:
                                raise RuntimeError(f"{mode} exited; see {log.name}")
                            try:
                                state = request(url + "/ui/api/settings")
                                break
                            except (OSError, ValueError):
                                time.sleep(.1)
                        else:
                            raise RuntimeError(f"{mode} readiness timeout; see {log.name}")
                        settings = state["config"]["server"]
                        assert settings["fast"] == (not args.standard) and settings["cacheSize"] == 0, settings
                        assert settings["workersPerLanguage"] == 0, settings

                        def translate(index):
                            start = time.perf_counter()
                            row = corpus[index]
                            text = request(url + "/translate", {key: row[key] for key in ("from", "to", "text")})["result"]
                            elapsed = (time.perf_counter() - start) * 1000
                            assert isinstance(text, str) and text, (mode, index, text)
                            if index in expected:
                                assert text == expected[index], (mode, index, text, expected[index])
                            return elapsed, text

                        for index in range(len(corpus)):
                            _, text = translate(index)
                            expected.setdefault(index, text)
                        report["expected_responses"] = [expected[i] for i in range(len(corpus))]
                        model_graphs = graphs(args.model_dir)
                        if "model_graphs" in report:
                            assert model_graphs == report["model_graphs"], "model graphs changed between bundles"
                        report["model_graphs"] = model_graphs

                        def runtimes():
                            engines = request(url + "/ui/api/settings")["engines"]
                            assert {(r["from"], r["to"]) for r in engines} == {("en", "zh-Hans"), ("zh-Hans", "en")}, engines
                            assert all(r["provider"] == "cpu" and ((r.get("runtime") == "native-async") == (mode == "candidate")) for r in engines), engines
                            return engines

                        runtimes()
                        for group, indices in groups:
                            for concurrency in (1, 8, 16):
                                jobs = indices * (3 if concurrency == 1 else 12)
                                before = process_stats(proc.pid)
                                started_at = now()
                                with concurrent.futures.ThreadPoolExecutor(concurrency) as executor:
                                    start = time.perf_counter()
                                    results = list(executor.map(translate, jobs))
                                    elapsed = time.perf_counter() - start
                                ended_at = now()
                                after = process_stats(proc.pid)
                                latencies = sorted(latency for latency, _ in results)
                                row = {
                                    "block": block, "mode": mode, "group": group, "concurrency": concurrency,
                                    "started_at": started_at, "ended_at": ended_at, "requests": len(jobs),
                                    "elapsed_seconds": elapsed, "rps": len(jobs) / elapsed,
                                    "p50_ms": statistics.median(latencies), "p95_ms": latencies[math.ceil(.95 * len(latencies)) - 1],
                                    "cpu_seconds": after["cpu_seconds"] - before["cpu_seconds"],
                                    "cpu_cores": (after["cpu_seconds"] - before["cpu_seconds"]) / elapsed,
                                    "rss_bytes": after["rss_bytes"], "peak_rss_bytes": after["peak_rss_bytes"],
                                    "cpus_allowed_list": after["cpus_allowed_list"], "runtimes": runtimes(),
                                    "all_responses_exact": True,
                                }
                                report["records"].append(row)
                                save()
                                print(json.dumps({k: v for k, v in row.items() if k != "runtimes"}), flush=True)
                        assert graphs(args.model_dir) == model_graphs, "model graphs changed during measurement"
                    finally:
                        proc.terminate()
                        try:
                            proc.wait(timeout=15)
                        except subprocess.TimeoutExpired:
                            proc.kill()
                            proc.wait()
            for mode, info in artifacts.items():
                assert sha(getattr(args, mode)) == info["bundle"]["sha256"], "bundle changed during benchmark"
                assert sha(pathlib.Path(info["library"])) == info["ort_library_sha256"], "ORT library changed"
                if info.get("native"):
                    assert sha(pathlib.Path(info["native"])) == info["native_sha256"], "native addon changed"
            report["completed"] = True
        except BaseException as error:
            report["error"] = f"{type(error).__name__}: {error}"
            raise
        finally:
            report["finished_at"] = now()
            save()


if __name__ == "__main__":
    main()
