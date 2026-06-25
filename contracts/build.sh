#!/usr/bin/env bash
# =============================================================================
# SikaChain — contract build script
# =============================================================================
# One-command build of all 7 system contracts. Requires Antelope CDT 4.1.0+
# installed (either as a system package or built from source with
# CDT_BUILD_PATH set in the environment).
#
# Usage:
#     ./build.sh                  # Release build, no tests
#     ./build.sh debug            # Debug build (for test-suite debugging only;
#                                  # contract WASM is identical to Release)
#     ./build.sh --with-tests     # Also build the Spring-based test suite
#                                  # (requires SPRING_BUILD_PATH)
#     ./build.sh clean            # Wipe the build/ directory
#
# Output:
#     build/contracts/<name>/<name>.wasm
#     build/contracts/<name>/<name>.abi
# =============================================================================

set -euo pipefail

cd "$(dirname "$0")"

BUILD_TYPE="Release"
BUILD_TESTS="OFF"

for arg in "$@"; do
   case "$arg" in
      debug)         BUILD_TYPE="Debug" ;;
      release)       BUILD_TYPE="Release" ;;
      --with-tests)  BUILD_TESTS="ON" ;;
      clean)
         rm -rf build
         echo "Cleaned build/"
         exit 0
         ;;
      *)
         echo "Unknown argument: $arg" >&2
         echo "Usage: $0 [debug|release] [--with-tests] [clean]" >&2
         exit 1
         ;;
   esac
done

# Sanity check that CDT is available
if ! command -v cdt-cpp >/dev/null 2>&1; then
   echo "ERROR: cdt-cpp not found in PATH." >&2
   echo "Install Antelope CDT 4.1.0+:" >&2
   echo "  https://github.com/AntelopeIO/cdt/releases" >&2
   echo "Or set CDT_BUILD_PATH to a source build." >&2
   exit 1
fi

mkdir -p build
cd build

CMAKE_ARGS=(
   -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
   -DBUILD_TESTS="$BUILD_TESTS"
)

if [[ "${SIKACHAIN:-}" == "1" ]]; then
   CMAKE_ARGS+=( -DSIKACHAIN=ON )
   echo "Building contracts with SIKACHAIN=1 (SYSTEM account = sika)"
fi

if [[ -n "${CDT_BUILD_PATH:-}" ]]; then
   CMAKE_ARGS+=( -Dcdt_DIR="$CDT_BUILD_PATH/lib/cmake/cdt" )
fi
if [[ "$BUILD_TESTS" == "ON" && -n "${SPRING_BUILD_PATH:-}" ]]; then
   CMAKE_ARGS+=( -Dspring_DIR="$SPRING_BUILD_PATH/lib/cmake/spring" )
fi

cmake "${CMAKE_ARGS[@]}" ..

# Determine job count portably
if command -v nproc >/dev/null 2>&1; then
   JOBS="$(nproc)"
elif command -v sysctl >/dev/null 2>&1; then
   JOBS="$(sysctl -n hw.ncpu)"
else
   JOBS=2
fi

make -j "$JOBS"

# Pretty summary
echo
echo "==================== BUILD COMPLETE ===================="
echo
for c in sika.system sika.token sika.rep sika.guard sika.rules sika.issue sika.treas; do
   wasm="contracts/$c/$c.wasm"
   abi="contracts/$c/$c.abi"
   if [[ -f "$wasm" && -f "$abi" ]]; then
      size=$(stat -c '%s' "$wasm" 2>/dev/null || stat -f '%z' "$wasm")
      printf "  %-15s ✓ %s.wasm (%s bytes), %s.abi\n" "$c" "$c" "$size" "$c"
   else
      printf "  %-15s ✗ MISSING WASM OR ABI\n" "$c"
   fi
done
echo
echo "Artifacts at:  $(pwd)/contracts/<name>/"
echo "Deploy with:   cleos set contract <account> <path> <name>.wasm <name>.abi"
echo "========================================================"
