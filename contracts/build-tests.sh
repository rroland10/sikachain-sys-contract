#!/usr/bin/env bash
# Build and run SikaChain contract tests via Spring's in-tree test target.
set -euo pipefail

SPRING="$(cd "$(dirname "$0")/../../AntelopeOS/spring" && pwd)"
CONTRACTS="$(cd "$(dirname "$0")" && pwd)"
BUILD="${CONTRACTS}/build"

rebuild_wasm() {
  if command -v cdt-cpp >/dev/null 2>&1; then
    echo "Building contract WASMs (local CDT)..."
    (cd "${CONTRACTS}" && bash build.sh)
    return
  fi
  if docker image inspect quorum-cdt:latest >/dev/null 2>&1; then
    echo "Building contract WASMs (Docker quorum-cdt)..."
    docker run --rm --platform linux/amd64 \
      -v "${CONTRACTS}:/work" -w /work/build quorum-cdt:latest \
      bash -c "apt-get update -qq && apt-get install -y -qq make cmake >/dev/null 2>&1; cmake --build . -j4"
    return
  fi
  if [[ ! -f "${BUILD}/contracts/sika.system/sika.system.wasm" ]]; then
    echo "error: missing WASMs — install CDT or build with Docker quorum-cdt:latest"
    exit 1
  fi
  echo "Using existing WASMs (install CDT or quorum-cdt to rebuild after source changes)"
}

rebuild_wasm

echo "Building sika_unit_tests in Spring..."
make -C "${SPRING}/build" sika_unit_tests -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo ""
"${SPRING}/build/sikachain-tests/sika_unit_tests" --log_level=test_suite --color_output=true
