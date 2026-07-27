add_library(contract-navigation
    src/Navigation.cpp
    src/Pathfinding.cpp)
add_library(OpenContract::navigation ALIAS contract-navigation)

target_include_directories(contract-navigation PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-navigation PUBLIC OpenContract::core)
contract_enable_strict_warnings(contract-navigation)
