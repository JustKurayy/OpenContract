add_library(contract-runtime-support
    src/ExplorationObjectiveSystem.cpp
    src/PlayerController.cpp
    src/Runtime.cpp
    src/RuntimeCommand.cpp
    src/RuntimeEventJournal.cpp
    src/RuntimeHost.cpp
    src/RuntimeObservation.cpp
    src/RuntimeSession.cpp
    src/RuntimeSystem.cpp
    src/RuntimeWorld.cpp
    src/SimulationClock.cpp)
add_library(OpenContract::runtime-support ALIAS contract-runtime-support)

target_include_directories(contract-runtime-support PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-runtime-support PUBLIC
    OpenContract::collision
    OpenContract::diagnostics
    OpenContract::installation
    OpenContract::modding)
contract_enable_strict_warnings(contract-runtime-support)
