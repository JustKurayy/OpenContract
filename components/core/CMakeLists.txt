add_library(contract-core INTERFACE)
add_library(OpenContract::core ALIAS contract-core)

target_include_directories(contract-core INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
