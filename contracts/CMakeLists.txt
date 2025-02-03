cmake_minimum_required(VERSION 3.5)

project(contracts)

option(SYSTEM_CONFIGURABLE_WASM_LIMITS
       "Enables use of the host functions activated by the CONFIGURABLE_WASM_LIMITS protocol feature" ON)

option(SYSTEM_BLOCKCHAIN_PARAMETERS
       "Enables use of the host functions activated by the BLOCKCHAIN_PARAMETERS protocol feature" ON)

find_package(cdt)

# system contract
# ---------------
add_contract(system system ${CMAKE_CURRENT_SOURCE_DIR}/system.entry.cpp)
target_include_directories(system  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(system PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

# token contract
# ---------------
add_contract(token token ${CMAKE_CURRENT_SOURCE_DIR}/token.entry.cpp)
target_include_directories(token  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(token PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

