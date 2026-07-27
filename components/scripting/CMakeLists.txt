add_library(contract-scripting INTERFACE)
add_library(OpenContract::scripting ALIAS contract-scripting)

target_include_directories(contract-scripting INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
