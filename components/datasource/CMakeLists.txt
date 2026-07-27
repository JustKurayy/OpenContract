add_library(contract-datasource
    src/DataSource.cpp)
add_library(OpenContract::datasource ALIAS contract-datasource)

target_include_directories(contract-datasource PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-datasource PUBLIC OpenContract::core)
contract_enable_strict_warnings(contract-datasource)
