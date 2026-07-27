add_library(contract-format-inspector
    src/Inspector.cpp)
add_library(OpenContract::format-inspector ALIAS contract-format-inspector)

target_include_directories(contract-format-inspector PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-format-inspector
    PUBLIC
        OpenContract::formats
        OpenContract::installation)
contract_enable_strict_warnings(contract-format-inspector)
