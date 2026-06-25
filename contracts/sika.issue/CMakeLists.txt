cmake_minimum_required(VERSION 3.5)
project(sika_issue)

add_contract(
   sika.issue sika.issue
   ${CMAKE_CURRENT_SOURCE_DIR}/src/sika.issue.cpp
)

target_include_directories(sika.issue PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

set_target_properties(sika.issue PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/contracts/sika.issue
)
