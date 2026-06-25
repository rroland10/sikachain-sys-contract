# =============================================================================
# SikaChain — system contracts
# =============================================================================
# Top-level CMake. Finds the Antelope CDT (Contract Development Toolkit) and
# builds all 7 system contracts as WebAssembly modules.
#
# Build:
#     mkdir build && cd build
#     cmake -DCMAKE_BUILD_TYPE=Release ..
#     make -j $(nproc)
#
# Or just use ../build.sh which handles the full flow.
#
# Artifacts produced in build/contracts/<name>/:
#     sika.system.wasm  + sika.system.abi
#     sika.token.wasm   + sika.token.abi
#     sika.rep.wasm     + sika.rep.abi
#     sika.guard.wasm   + sika.guard.abi
#     sika.rules.wasm   + sika.rules.abi
#     sika.issue.wasm   + sika.issue.abi
# =============================================================================

cmake_minimum_required(VERSION 3.5)
project(sikachain-contracts VERSION 0.1.0)

# SIKACHAIN=ON: privileged system account is `sika` (Spring -DSIKACHAIN=ON).
# Default OFF keeps `eosio` for standard Spring dev chains.
option(SIKACHAIN "Build for SikaChain (SYSTEM account = sika)" OFF)
if(SIKACHAIN)
   add_definitions(-DSIKACHAIN)
   message(STATUS "SIKACHAIN enabled: SYSTEM account = sika")
endif()

# Find the CDT package. CDT 4.1.0+ is required.
find_package(cdt REQUIRED)

# If CDT_BUILD_PATH was set, use it (for source builds of CDT).
if(DEFINED ENV{CDT_BUILD_PATH})
   set(cdt_DIR "$ENV{CDT_BUILD_PATH}/lib/cmake/cdt")
endif()

# Each contract is a self-contained subdirectory with its own CMakeLists.
add_subdirectory(sika.system)
add_subdirectory(sika.token)
add_subdirectory(sika.rep)
add_subdirectory(sika.guard)
add_subdirectory(sika.rules)
add_subdirectory(sika.issue)
add_subdirectory(sika.treas)

# Contract tests use Spring's EosioTester — run contracts/build-tests.sh
option(BUILD_TESTS "Build the contract test suite (requires Spring)" OFF)
if(BUILD_TESTS)
   message(STATUS "Use contracts/build-tests.sh to build and run the test suite")
endif()
