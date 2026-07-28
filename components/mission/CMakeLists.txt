add_library(contract-mission
    src/Mission.cpp
    src/SourceSceneBuilder.cpp
    src/SourceMissionLoader.cpp)
add_library(OpenContract::mission ALIAS contract-mission)

target_include_directories(contract-mission PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-mission PUBLIC
    OpenContract::core
    OpenContract::datasource
    OpenContract::formats
    OpenContract::scene)
contract_enable_strict_warnings(contract-mission)
