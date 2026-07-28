if(NOT TARGET bgfx::shaderc)
    message(FATAL_ERROR
        "The pinned bgfx dependency does not expose shaderc")
endif()

set(contract_shader_output
    "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
set(contract_shader_varying
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc")
bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vs_contract_texture.sc"
    VARYING_DEF "${contract_shader_varying}"
    OUTPUT_DIR "${contract_shader_output}"
    OUT_FILES_VAR contract_vertex_shader
    INCLUDE_DIRS "${BGFX_DIR}/src"
    PROFILES s_5_0
    AS_HEADERS)
bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fs_contract_texture.sc"
    VARYING_DEF "${contract_shader_varying}"
    OUTPUT_DIR "${contract_shader_output}"
    OUT_FILES_VAR contract_fragment_shader
    INCLUDE_DIRS "${BGFX_DIR}/src"
    PROFILES s_5_0
    AS_HEADERS)

add_library(contract-rendering-bgfx
    ${contract_vertex_shader}
    ${contract_fragment_shader}
    src/BgfxRenderer.cpp
    src/FreeCamera.cpp
    src/SceneFraming.cpp
    src/WireframeIndexBuilder.cpp)
add_library(
    OpenContract::rendering-bgfx
    ALIAS contract-rendering-bgfx)

target_include_directories(contract-rendering-bgfx PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_include_directories(contract-rendering-bgfx PRIVATE
    "${contract_shader_output}/dxbc")
target_link_libraries(contract-rendering-bgfx
    PUBLIC OpenContract::runtime-support
    PRIVATE
        ${CONTRACT_BGFX_TARGET}
        ${CONTRACT_BX_TARGET})
contract_enable_strict_warnings(contract-rendering-bgfx)
