# Pull in CDT's contract-building macros
add_contract(
   sika.system sika.system
   ${CMAKE_CURRENT_SOURCE_DIR}/src/sika.system.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/native.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/producer_pay.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/voting_schedule.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/enforcement.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/rex.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/ram_bancor.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/exchange_state.cpp
)

target_include_directories(sika.system PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

set_target_properties(sika.system PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/contracts/sika.system
)

target_ricardian_directory(sika.system ${CMAKE_CURRENT_SOURCE_DIR}/ricardian)
