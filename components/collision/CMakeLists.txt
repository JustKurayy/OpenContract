add_library(contract-collision
    src/StaticCollisionWorld.cpp)
add_library(OpenContract::collision ALIAS contract-collision)

target_include_directories(contract-collision PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-collision PUBLIC
    OpenContract::core
    OpenContract::scene)
contract_enable_strict_warnings(contract-collision)
