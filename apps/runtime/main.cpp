#include <contract/diagnostics/DiagnosticSink.hpp>
#include <contract/filesystem/ReadOnlyFilesystem.hpp>
#include <contract/installation/Installation.hpp>
#include <contract/mission/SourceMissionLoader.hpp>
#include <contract/platform/NativeLoadingDisplay.hpp>
#include <contract/platform/NativeRuntimeRunner.hpp>
#include <contract/runtime/Runtime.hpp>

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    const auto options = contract::runtime::parse_runtime_options(
        arguments,
        std::cerr);
    if (!options.has_value()) {
        return static_cast<int>(contract::runtime::RuntimeExitCode::usage_error);
    }
    if (options->help) {
        contract::runtime::print_runtime_help(std::cout);
        return static_cast<int>(contract::runtime::RuntimeExitCode::success);
    }
    const contract::filesystem::NativeReadOnlyFilesystem filesystem;
    contract::diagnostics::DiagnosticBuffer diagnostics;
    contract::platform::NativeRuntimeRunner runner;
    contract::platform::NativeLoadingDisplay loading_display;
    const contract::mission::ReadOnlySourceMissionLoader
        source_mission_loader;
    const contract::runtime::RuntimeContext context{
        filesystem,
        diagnostics,
        contract::installation::default_recognition_policy(),
        contract::installation::environment_game_path(),
        contract::installation::configured_game_path(),
        contract::installation::default_windows_probe_paths(),
        &runner,
        &source_mission_loader,
        &loading_display};
    return contract::runtime::run_runtime(
        *options,
        context,
        std::cout,
        std::cerr);
}
