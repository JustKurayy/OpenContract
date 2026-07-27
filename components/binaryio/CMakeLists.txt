add_library(contract-binaryio INTERFACE)
add_library(OpenContract::binaryio ALIAS contract-binaryio)

target_include_directories(contract-binaryio INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-binaryio INTERFACE OpenContract::core)
