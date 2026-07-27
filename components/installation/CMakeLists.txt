configure_file(
    include/contract/installation/BuildConfiguration.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/contract/installation/BuildConfiguration.hpp"
    @ONLY)

add_library(contract-installation
    src/Installation.cpp)
add_library(OpenContract::installation ALIAS contract-installation)

target_include_directories(contract-installation PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>")
target_link_libraries(contract-installation
    PUBLIC
        OpenContract::core
        OpenContract::diagnostics
        OpenContract::filesystem)
contract_enable_strict_warnings(contract-installation)
