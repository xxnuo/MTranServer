#!/usr/bin/env bash
set -euo pipefail

# NODE_INCLUDE_DIR comes from scripts/prepare-node-headers.ts; no Node executable is needed.
# Usage: NODE_INCLUDE_DIR=/path/to/include/node bash scripts/build-portable-native.sh [output-directory]
fail() { echo "[portable-native] $*" >&2; exit 1; }

case "$(uname -m)" in
  x86_64) arch=x64; docker_arch=amd64; baseline=(-march=x86-64 -mtune=generic) ;;
  aarch64|arm64) arch=arm64; docker_arch=arm64; baseline=(-march=armv8-a) ;;
  *) fail "Only native Linux x64/arm64 builders are supported" ;;
esac
[[ $(uname -s) == Linux ]] || fail "A native Linux runner is required"

if [[ ${1:-} != --container ]]; then
  [[ $# -le 1 ]] || fail "Usage: NODE_INCLUDE_DIR=... $0 [output-directory]"
  root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
  [[ -n ${NODE_INCLUDE_DIR:-} && -f $NODE_INCLUDE_DIR/node_api.h ]] || fail "Set NODE_INCLUDE_DIR to prepared Node headers (include/node)"
  headers=$(cd -- "$NODE_INCLUDE_DIR" && pwd -P)
  engine_arch=$(docker info --format '{{.Architecture}}')
  case "$arch:$engine_arch" in
    x64:x86_64|x64:amd64|arm64:aarch64|arm64:arm64) ;;
    *) fail "Docker engine architecture $engine_arch differs from native $arch; emulation is unsupported" ;;
  esac
  output=${1:-"$root/dist"}
  mkdir -p -- "$output" "$root/.tmp"
  output=$(cd -- "$output" && pwd -P)
  context=$(mktemp -d "$root/.tmp/portable-context-XXXXXX")
  trap 'rm -rf -- "$context"' EXIT
  image="mtran-native-portable:$arch"
  docker build --platform "linux/$docker_arch" --build-arg "TARGETARCH=$docker_arch" \
    -f "$root/native/ssru/Dockerfile.portable" -t "$image" "$context"
  docker run --rm --platform "linux/$docker_arch" --network none --user "$(id -u):$(id -g)" \
    --mount "type=bind,src=$root,dst=/src,readonly" \
    --mount "type=bind,src=$headers,dst=/node-headers,readonly" \
    --mount "type=bind,src=$output,dst=/out" \
    -e "PORTABLE_NATIVE_ARCH=$arch" "$image"
  exit 0
fi

[[ ${PORTABLE_NATIVE_ARCH:-} == "$arch" ]] || fail "Container architecture differs from the native runner"
[[ $(getconf GNU_LIBC_VERSION) == 'glibc 2.28' ]] || fail "Builder must use glibc 2.28"
[[ -f /node-headers/node_api.h ]] || fail "Node headers are missing"
build=$(mktemp -d /out/.portable-build-XXXXXX)
trap 'rm -rf -- "$build"' EXIT
export TMPDIR="$build"
common=(-std=c++20 -O3 -Wall -Wextra -fPIC -pthread -ffp-contract=off "${baseline[@]}")
packed=(-DMTRAN_SSRU_PACKED "$build/packed.o")
vnni=()
c++ --version | head -n 1
if [[ $arch == x64 ]]; then
  # Only isolated TUs enable VNNI/AVX-512 VNNI (no LTO); packed.cc performs runtime CPU dispatch.
  c++ "${common[@]}" -mavx2 -mavxvnni -mfma -isystem native/ssru/third_party/xsimd/include \
    -c native/ssru/packed_vnni.cc -o "$build/packed-vnni.o"
  vnni=(-DMTRAN_SSRU_HAVE_VNNI)
  packed+=("$build/packed-vnni.o")
  if c++ "${common[@]}" -mavx512f -mavx512bw -mavx512dq -mavx512vl -mavx512vnni -mfma \
    -c native/ssru/packed_avx512vnni.cc -o "$build/packed-avx512vnni.o"; then
    vnni+=(-DMTRAN_SSRU_HAVE_AVX512VNNI)
    packed+=("$build/packed-avx512vnni.o")
  fi
fi
c++ "${common[@]}" "${vnni[@]}" -c native/ssru/packed.cc -o "$build/packed.o"
artifact="$build/onnx-async-linux-$arch.node"
c++ "${common[@]}" -shared \
  -I /node-headers -I native/onnx-async -DMTRAN_NATIVE_SSRU \
  native/onnx-async/addon.cc native/onnx-async/ssru_op.cc \
  native/ssru/ssru.cc native/ssru/encoder.cc native/ssru/ops.cc native/ssru/pcore_pool.cc \
  "${packed[@]}" -ldl -o "$artifact"

# Reject an ABI regression before replacing an existing output artifact.
readelf --version-info --wide "$artifact" > "$build/versions.txt"
readelf --dynamic --wide "$artifact" > "$build/dynamic.txt"
grep -q GLIBC_PRIVATE "$build/versions.txt" && fail "Unexpected private glibc dependency"
gcc_baseline=GCC_3.0
[[ $arch != arm64 ]] || gcc_baseline=GCC_4.5.0
for requirement in GLIBCXX_3.4.22 CXXABI_1.3.11 "$gcc_baseline"; do
  family=${requirement%%_*}
  actual=$(grep -oE "${family}_[0-9]+(\.[0-9]+)*" "$build/versions.txt" | sort -Vu | tail -n 1)
  [[ -z $actual || $(printf '%s\n' "$actual" "$requirement" | sort -V | tail -n 1) == "$requirement" ]] || fail "Requires $actual, newer than ORT's $requirement baseline"
done
maximum=$(grep -oE 'GLIBC_[0-9]+(\.[0-9]+)*' "$build/versions.txt" | sort -Vu | tail -n 1)
[[ $(printf '%s\n' "$maximum" GLIBC_2.28 | sort -V | tail -n 1) == GLIBC_2.28 ]] || fail "Requires $maximum, newer than GLIBC_2.28"
if grep -Eq '\((RPATH|RUNPATH)\)' "$build/dynamic.txt"; then
  fail "Unexpected build-time library search path"
fi
mv -f -- "$artifact" "/out/onnx-async-linux-$arch.node"
echo "[portable-native] Built onnx-async-linux-$arch.node; maximum $maximum; C++ runtime requirements bounded by ORT 1.30"
