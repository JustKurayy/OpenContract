include(FetchContent)

find_package(nlohmann_json 3.12.0 CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
        DOWNLOAD_EXTRACT_TIMESTAMP OFF)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

if(CONTRACT_BUILD_RUNTIME OR CONTRACT_BUILD_TESTS)
    find_package(bgfx CONFIG QUIET)
    if(TARGET bgfx::bgfx)
        set(CONTRACT_BGFX_TARGET bgfx::bgfx)
    else()
        set(BGFX_AMALGAMATED OFF CACHE BOOL "" FORCE)
        set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
        set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
        set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
        set(BGFX_CONFIG_VIDEO OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            bgfx_cmake
            GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
            GIT_TAG 5b418ad60dc4445a56e4b11f6cf5c8f27e137372
            GIT_PROGRESS TRUE
            GIT_SUBMODULES_RECURSE TRUE)
        FetchContent_MakeAvailable(bgfx_cmake)
        set(CONTRACT_BGFX_TARGET bgfx)
    endif()
endif()
