add_contract(
   sika.guard sika.guard
   ${CMAKE_CURRENT_SOURCE_DIR}/src/sika.guard.cpp
)

target_include_directories(sika.guard PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(sika.guard PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/contracts/sika.guard
)
