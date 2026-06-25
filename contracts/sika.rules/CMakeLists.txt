cmake_minimum_required(VERSION 3.5)
project(sika_rules)

add_contract(
   sika.rules sika.rules
   ${CMAKE_CURRENT_SOURCE_DIR}/src/sika.rules.cpp
)

target_include_directories(sika.rules PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

set_target_properties(sika.rules PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/contracts/sika.rules
)
