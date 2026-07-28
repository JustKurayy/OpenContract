set(contract_shader_source
    "${BGFX_DIR}/examples/runtime/shaders/dxbc")
if(NOT EXISTS "${contract_shader_source}/vs_cubes.bin"
   OR NOT EXISTS "${contract_shader_source}/fs_cubes.bin")
    message(FATAL_ERROR
        "Pinned bgfx D3D11 example shaders are unavailable")
endif()

file(READ
    "${contract_shader_source}/vs_cubes.bin"
    contract_vertex_shader_hex
    HEX)
file(READ
    "${contract_shader_source}/fs_cubes.bin"
    contract_fragment_shader_hex
    HEX)
string(REGEX REPLACE
    "(..)"
    "0x\\1,"
    contract_vertex_shader_bytes
    "${contract_vertex_shader_hex}")
string(REGEX REPLACE
    "(..)"
    "0x\\1,"
    contract_fragment_shader_bytes
    "${contract_fragment_shader_hex}")

set(contract_shader_output
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${contract_shader_output}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/EmbeddedShaders.hpp.in"
    "${contract_shader_output}/EmbeddedShaders.hpp"
    @ONLY)

add_library(contract-rendering-bgfx
    src/BgfxRenderer.cpp
    src/FreeCamera.cpp
    src/WireframeIndexBuilder.cpp)
add_library(
    OpenContract::rendering-bgfx
    ALIAS contract-rendering-bgfx)

target_include_directories(contract-rendering-bgfx PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_include_directories(contract-rendering-bgfx PRIVATE
    "${contract_shader_output}")
target_link_libraries(contract-rendering-bgfx
    PUBLIC OpenContract::runtime-support
    PRIVATE
        ${CONTRACT_BGFX_TARGET}
        ${CONTRACT_BX_TARGET})
contract_enable_strict_warnings(contract-rendering-bgfx)
