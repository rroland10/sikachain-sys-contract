cmake_minimum_required(VERSION 3.5)
project(sika_treas)

add_contract(
   sika.treas sika.treas
   ${CMAKE_CURRENT_SOURCE_DIR}/src/sika.treas.cpp
)

target_include_directories(sika.treas PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

set_target_properties(sika.treas PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/contracts/sika.treas
)

target_ricardian_directory(sika.treas ${CMAKE_CURRENT_SOURCE_DIR}/ricardian)
