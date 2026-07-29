add_library(contract-platform-runtime
    src/NativeLoadingDisplay.cpp
    src/NativeRuntimeRunner.cpp)
add_library(OpenContract::platform-runtime ALIAS contract-platform-runtime)

target_include_directories(contract-platform-runtime PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-platform-runtime PUBLIC
    OpenContract::runtime-support
    PRIVATE OpenContract::rendering-bgfx)
if(WIN32)
    target_link_libraries(
        contract-platform-runtime
        PRIVATE
            gdi32
            user32)
endif()
contract_enable_strict_warnings(contract-platform-runtime)
