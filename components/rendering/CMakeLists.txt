add_library(contract-rendering-bgfx
    src/BgfxRenderer.cpp)
add_library(
    OpenContract::rendering-bgfx
    ALIAS contract-rendering-bgfx)

target_include_directories(contract-rendering-bgfx PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-rendering-bgfx
    PUBLIC OpenContract::runtime-support
    PRIVATE ${CONTRACT_BGFX_TARGET})
contract_enable_strict_warnings(contract-rendering-bgfx)
