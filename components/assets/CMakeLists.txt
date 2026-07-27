add_library(contract-assets
    src/Asset.cpp
    src/AssetLocator.cpp)
add_library(OpenContract::assets ALIAS contract-assets)

target_include_directories(contract-assets PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-assets PUBLIC
    OpenContract::core
    OpenContract::filesystem)
contract_enable_strict_warnings(contract-assets)
