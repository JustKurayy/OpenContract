add_library(contract-binaryio
    src/SourceBinaryReader.cpp)
add_library(OpenContract::binaryio ALIAS contract-binaryio)

target_include_directories(contract-binaryio PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-binaryio PUBLIC
    OpenContract::core
    OpenContract::datasource)
contract_enable_strict_warnings(contract-binaryio)
