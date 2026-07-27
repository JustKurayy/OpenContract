add_library(contract-mod-validator-tool
    src/ModValidator.cpp)
add_library(OpenContract::mod-validator-tool ALIAS contract-mod-validator-tool)

target_include_directories(contract-mod-validator-tool PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-mod-validator-tool PUBLIC OpenContract::modding)
contract_enable_strict_warnings(contract-mod-validator-tool)
