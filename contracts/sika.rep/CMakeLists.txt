cmake_minimum_required(VERSION 3.5)
project(sika_rep)

add_contract(
   sika.rep sika.rep
   ${CMAKE_CURRENT_SOURCE_DIR}/src/sika.rep.cpp
)

target_include_directories(sika.rep PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

set_target_properties(sika.rep PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/contracts/sika.rep
)
