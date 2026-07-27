add_library(contract-modding
    src/ModManifest.cpp
    src/ModPackage.cpp
    src/PackageResolver.cpp
    src/PackageSet.cpp)
add_library(OpenContract::modding ALIAS contract-modding)

target_include_directories(contract-modding PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-modding PUBLIC
    OpenContract::assets
    OpenContract::core
    OpenContract::mission
    OpenContract::navigation
    OpenContract::scene)
target_link_libraries(contract-modding PRIVATE
    OpenContract::datasource
    nlohmann_json::nlohmann_json)
contract_enable_strict_warnings(contract-modding)
