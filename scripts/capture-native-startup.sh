#!/usr/bin/env bash
set -euo pipefail
# Use the failed HTTP report's already-prepared models directory, which includes manifest.json.
[[ $# == 5 ]] || { echo 'Usage: capture-startup.sh ELECTRON MAIN_JS PREPARED_MODELS RECORDS_JSON OUTPUT_DIR' >&2; exit 2; }
electron=$(realpath -- "$1")
entry=$(realpath -- "$2")
models=$(realpath -- "$3")
records=$(realpath -- "$4")
mkdir -p -- "$5"
output=$(realpath -- "$5")
[[ ! -e "$output/config" ]] || { echo 'Use a fresh output directory' >&2; exit 2; }
mkdir -p -- "$output/config" "$output/home" "$output/logs"
cp -- "$records" "$output/config/records.json"
export HOME="$output/home" USERPROFILE="$output/home" ELECTRON_RUN_AS_NODE=1
export MT_CPU_ONLY=true MT_WORKERS_PER_LANGUAGE=2 UV_THREADPOOL_SIZE=4 DEBUGINFOD_URLS=
unset MT_NATIVE_SSRU NODE_OPTIONS NODE_PATH
# The time limit bounds a successful startup; SIGSEGV stops gdb first and prints all native threads.
exec timeout --signal=TERM --kill-after=5s 210s gdb --batch \
  -ex 'set pagination off' -ex 'set debuginfod enabled off' \
  -ex 'handle SIGPIPE nostop noprint pass' -ex 'handle SIGTERM nostop noprint pass' \
  -ex run -ex 'thread apply all bt full' -ex 'info sharedlibrary' -ex 'info proc mappings' \
  --args "$electron" "$entry" --host 127.0.0.1 --port 0 --config-dir "$output/config" \
  --model-dir "$models" --log-dir "$output/logs" --fast --offline --cache-size 0 \
  --no-check-update --ui --log-console --log-level debug --worker-idle-timeout 600 \
  > "$output/gdb.log" 2>&1
