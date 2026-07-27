add_library(contract-formats
    src/FormatProbe.cpp)
add_library(OpenContract::formats ALIAS contract-formats)

target_include_directories(contract-formats PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-formats PUBLIC OpenContract::datasource)
contract_enable_strict_warnings(contract-formats)
