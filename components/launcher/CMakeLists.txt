add_library(contract-launcher-support
    src/Launcher.cpp)
add_library(OpenContract::launcher-support ALIAS contract-launcher-support)

target_include_directories(contract-launcher-support PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-launcher-support PUBLIC
    OpenContract::installation)
contract_enable_strict_warnings(contract-launcher-support)
