function(contract_add_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    contract_enable_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

contract_add_test(contract-core-tests core/CoreTests.cpp)
target_link_libraries(contract-core-tests PRIVATE OpenContract::core)

contract_add_test(contract-diagnostics-tests diagnostics/DiagnosticTests.cpp)
target_link_libraries(contract-diagnostics-tests PRIVATE OpenContract::diagnostics)

contract_add_test(contract-datasource-tests datasource/DataSourceTests.cpp)
target_link_libraries(contract-datasource-tests PRIVATE OpenContract::datasource)

contract_add_test(contract-filesystem-tests filesystem/FilesystemTests.cpp)
target_link_libraries(contract-filesystem-tests PRIVATE OpenContract::filesystem)

contract_add_test(contract-installation-tests installation/InstallationTests.cpp)
target_link_libraries(contract-installation-tests PRIVATE OpenContract::installation)

contract_add_test(contract-binaryio-tests binaryio/BinaryReaderTests.cpp)
target_link_libraries(contract-binaryio-tests PRIVATE OpenContract::binaryio)

contract_add_test(contract-source-binary-reader-tests binaryio/SourceBinaryReaderTests.cpp)
target_link_libraries(contract-source-binary-reader-tests PRIVATE OpenContract::binaryio)

contract_add_test(contract-formats-tests formats/FormatProbeTests.cpp)
target_link_libraries(contract-formats-tests PRIVATE OpenContract::formats)

contract_add_test(contract-assets-tests assets/AssetTests.cpp)
target_link_libraries(contract-assets-tests PRIVATE OpenContract::assets)

contract_add_test(contract-asset-locator-tests assets/AssetLocatorTests.cpp)
target_link_libraries(contract-asset-locator-tests PRIVATE OpenContract::assets)

contract_add_test(contract-scene-tests scene/SceneTests.cpp)
target_link_libraries(contract-scene-tests PRIVATE OpenContract::scene)

contract_add_test(contract-navigation-tests navigation/NavigationTests.cpp)
target_link_libraries(contract-navigation-tests PRIVATE OpenContract::navigation)

contract_add_test(contract-pathfinding-tests navigation/PathfindingTests.cpp)
target_link_libraries(contract-pathfinding-tests PRIVATE OpenContract::navigation)

contract_add_test(contract-mission-tests mission/MissionTests.cpp)
target_link_libraries(contract-mission-tests PRIVATE OpenContract::mission)

contract_add_test(contract-modding-tests modding/ModModelTests.cpp)
target_link_libraries(contract-modding-tests PRIVATE OpenContract::modding)

contract_add_test(contract-manifest-tests modding/ModManifestTests.cpp)
target_link_libraries(contract-manifest-tests PRIVATE OpenContract::modding)

contract_add_test(contract-package-resolver-tests modding/PackageResolverTests.cpp)
target_link_libraries(contract-package-resolver-tests PRIVATE OpenContract::modding)

contract_add_test(contract-package-set-tests modding/PackageSetTests.cpp)
target_link_libraries(contract-package-set-tests PRIVATE OpenContract::modding)

contract_add_test(contract-package-asset-source-tests modding/PackageAssetSourceTests.cpp)
target_link_libraries(contract-package-asset-source-tests PRIVATE OpenContract::modding)

contract_add_test(contract-runtime-tests runtime/RuntimeTests.cpp)
target_link_libraries(contract-runtime-tests PRIVATE OpenContract::runtime-support)

contract_add_test(contract-runtime-world-tests runtime/RuntimeWorldTests.cpp)
target_link_libraries(contract-runtime-world-tests PRIVATE OpenContract::runtime-support)

contract_add_test(contract-runtime-command-tests runtime/RuntimeCommandTests.cpp)
target_link_libraries(contract-runtime-command-tests PRIVATE OpenContract::runtime-support)

contract_add_test(contract-runtime-event-journal-tests runtime/RuntimeEventJournalTests.cpp)
target_link_libraries(contract-runtime-event-journal-tests PRIVATE OpenContract::runtime-support)

contract_add_test(contract-simulation-clock-tests runtime/SimulationClockTests.cpp)
target_link_libraries(contract-simulation-clock-tests PRIVATE OpenContract::runtime-support)

contract_add_test(contract-runtime-session-tests runtime/RuntimeSessionTests.cpp)
target_link_libraries(contract-runtime-session-tests PRIVATE OpenContract::runtime-support)

contract_add_test(contract-runtime-system-tests runtime/RuntimeSystemTests.cpp)
target_link_libraries(contract-runtime-system-tests PRIVATE OpenContract::runtime-support)

contract_add_test(contract-runtime-observation-tests runtime/RuntimeObservationTests.cpp)
target_link_libraries(contract-runtime-observation-tests PRIVATE OpenContract::runtime-support)

contract_add_test(contract-runtime-host-tests runtime/RuntimeHostTests.cpp)
target_link_libraries(contract-runtime-host-tests PRIVATE OpenContract::runtime-support)

if(WIN32)
    contract_add_test(
        contract-native-runtime-runner-tests
        runtime/NativeRuntimeRunnerTests.cpp)
    target_link_libraries(
        contract-native-runtime-runner-tests
        PRIVATE OpenContract::platform-runtime)
endif()

contract_add_test(contract-launcher-tests launcher/LauncherTests.cpp)
target_link_libraries(contract-launcher-tests PRIVATE OpenContract::launcher-support)

if(CONTRACT_BUILD_TOOLS)
    contract_add_test(contract-inspector-tests tools/InspectorTests.cpp)
    target_link_libraries(contract-inspector-tests PRIVATE OpenContract::format-inspector)

    contract_add_test(contract-mod-validator-tests tools/ModValidatorTests.cpp)
    target_link_libraries(contract-mod-validator-tests PRIVATE OpenContract::mod-validator-tool)

    add_test(
        NAME contract-mod-validate-cli
        COMMAND
            $<TARGET_FILE:contract-mod-validate>
            --manifest
            "${CMAKE_CURRENT_SOURCE_DIR}/synthetic_test_data/minimal.contract.json"
            --json)

    add_test(
        NAME contract-boot-manifest-cli
        COMMAND
            $<TARGET_FILE:contract-mod-validate>
            --manifest
            "${CMAKE_CURRENT_SOURCE_DIR}/synthetic_test_data/boot.contract.json"
            --json)
endif()

if(CONTRACT_ENABLE_LOCAL_GAME_TESTS)
    add_executable(contract-local-game-tests installation/LocalGameReadOnlyTests.cpp)
    target_include_directories(contract-local-game-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(contract-local-game-tests PRIVATE OpenContract::installation)
    contract_enable_strict_warnings(contract-local-game-tests)
    add_test(NAME contract-local-game-tests COMMAND contract-local-game-tests)
    set_tests_properties(contract-local-game-tests PROPERTIES LABELS "local-game")
endif()
