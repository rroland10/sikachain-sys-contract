cmake_minimum_required(VERSION 3.5)
project(sika_token)

add_contract(
   sika.token sika.token
   ${CMAKE_CURRENT_SOURCE_DIR}/src/sika.token.cpp
)

target_include_directories(sika.token PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

set_target_properties(sika.token PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/contracts/sika.token
)

target_ricardian_directory(sika.token ${CMAKE_CURRENT_SOURCE_DIR}/ricardian)
