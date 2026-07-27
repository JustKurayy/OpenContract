add_library(contract-scene
    src/Scene.cpp)
add_library(OpenContract::scene ALIAS contract-scene)

target_include_directories(contract-scene PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-scene PUBLIC
    OpenContract::assets
    OpenContract::core
    OpenContract::navigation)
contract_enable_strict_warnings(contract-scene)
