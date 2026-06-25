cmake_minimum_required(VERSION 3.16)
project(sika_contract_tests)

set(SPRING_BUILD "$ENV{SPRING_BUILD_PATH}" CACHE PATH "Spring build directory")
if(NOT SPRING_BUILD)
   set(SPRING_BUILD "/Users/randallroland/Desktop/Projects/AntelopeOS/spring/build")
endif()

set(SIKA_CONTRACTS_BUILD "$ENV{SIKA_CONTRACTS_BUILD}" CACHE PATH "Compiled contract WASM output dir")
if(NOT SIKA_CONTRACTS_BUILD)
   set(SIKA_CONTRACTS_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/../build")
endif()

if(DEFINED ENV{SIKA_TESTS_DIR} AND EXISTS "$ENV{SIKA_TESTS_DIR}/sika_tester.hpp")
   set(SIKA_TESTS_DIR "$ENV{SIKA_TESTS_DIR}")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../sika chain testing script/sika_tester.hpp")
   set(SIKA_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../sika chain testing script")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../sika-chain-contract-tests/sika_tester.hpp")
   set(SIKA_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../sika-chain-contract-tests")
else()
   set(SIKA_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../sika chain testing script")
endif()

if(NOT EXISTS "${SIKA_TESTS_DIR}/sika_tester.hpp")
   message(FATAL_ERROR "Test sources not found at ${SIKA_TESTS_DIR} — clone https://github.com/rroland10/sika-chain-contract-tests")
endif()

foreach(c sika.system sika.token sika.rep sika.guard sika.rules sika.issue)
   if(NOT EXISTS "${SIKA_CONTRACTS_BUILD}/contracts/${c}/${c}.wasm")
      message(FATAL_ERROR "Missing ${SIKA_CONTRACTS_BUILD}/contracts/${c}/${c}.wasm — run ./build.sh first")
   endif()
endforeach()

include("${SPRING_BUILD}/lib/cmake/spring/EosioTester.cmake")

configure_file(
   ${CMAKE_CURRENT_SOURCE_DIR}/contracts.hpp.in
   ${CMAKE_CURRENT_BINARY_DIR}/contracts.hpp
   @ONLY
)

set(TEST_SOURCES
   ${CMAKE_CURRENT_SOURCE_DIR}/main_test.cpp
   ${SIKA_TESTS_DIR}/test_sika_inflation.cpp
   ${SIKA_TESTS_DIR}/test_sika_enforcement.cpp
   ${SIKA_TESTS_DIR}/test_sika_rep_boost.cpp
   ${SIKA_TESTS_DIR}/test_sika_ram_fee.cpp
   ${SIKA_TESTS_DIR}/test_sika_rex.cpp
   ${SIKA_TESTS_DIR}/test_sika_rules.cpp
   ${SIKA_TESTS_DIR}/test_sika_issue.cpp
   ${SIKA_TESTS_DIR}/test_sika_treas.cpp
)

add_eosio_test_executable(sika_unit_tests ${TEST_SOURCES})

target_include_directories(sika_unit_tests PRIVATE
   ${SIKA_TESTS_DIR}
   ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(NAME sika_unit_tests COMMAND sika_unit_tests --log_level=test_suite)

add_custom_target(run_sika_tests
   COMMAND sika_unit_tests --log_level=test_suite --color_output=true
   DEPENDS sika_unit_tests
   COMMENT "Running SikaChain contract test suite"
)
