add_library(contract-runtime-support
    src/Runtime.cpp
    src/RuntimeCommand.cpp
    src/RuntimeSession.cpp
    src/RuntimeWorld.cpp
    src/SimulationClock.cpp)
add_library(OpenContract::runtime-support ALIAS contract-runtime-support)

target_include_directories(contract-runtime-support PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-runtime-support PUBLIC
    OpenContract::diagnostics
    OpenContract::installation
    OpenContract::modding)
contract_enable_strict_warnings(contract-runtime-support)
