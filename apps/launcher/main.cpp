#include <contract/launcher/Launcher.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

#ifdef _WIN32
std::optional<std::wstring> utf8_to_wide(const std::string& value) {
    if (value.empty()) {
        return std::wstring{};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return std::nullopt;
    }
    std::wstring converted(static_cast<std::size_t>(required), L'\0');
    const int written = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        converted.data(),
        required);
    if (written != required) {
        return std::nullopt;
    }
    return converted;
}

class WindowsRuntimeProcess final : public contract::launcher::IRuntimeProcess {
public:
    int invoke(
        const std::filesystem::path& game_path,
        const std::vector<std::filesystem::path>& mod_manifests,
        const std::optional<std::string>& mission) const override {
        std::vector<wchar_t> module_path(32768);
        const DWORD length = GetModuleFileNameW(
            nullptr,
            module_path.data(),
            static_cast<DWORD>(module_path.size()));
        if (length == 0 ||
            static_cast<std::size_t>(length) >= module_path.size()) {
            std::cerr << "Unable to locate contract-runtime\n";
            return static_cast<int>(
                contract::launcher::LauncherExitCode::invocation_failed);
        }

        auto runtime_path = std::filesystem::path(module_path.data()).parent_path();
        runtime_path /= L"contract-runtime.exe";
        std::error_code path_error;
        if (!std::filesystem::is_regular_file(runtime_path, path_error) ||
            path_error) {
            std::cerr << "contract-runtime.exe was not found beside the launcher\n";
            return static_cast<int>(
                contract::launcher::LauncherExitCode::invocation_failed);
        }

        std::vector<std::wstring> arguments{
            L"--game-path",
            game_path.wstring()};
        for (const auto& manifest : mod_manifests) {
            arguments.push_back(L"--mod-manifest");
            arguments.push_back(manifest.wstring());
        }
        if (mission.has_value()) {
            const auto wide_mission = utf8_to_wide(*mission);
            if (!wide_mission.has_value()) {
                std::cerr << "Mission identifier is not valid UTF-8\n";
                return static_cast<int>(
                    contract::launcher::LauncherExitCode::invocation_failed);
            }
            arguments.push_back(L"--mission");
            arguments.push_back(*wide_mission);
        }

        auto command_line = contract::launcher::build_windows_command_line(
            runtime_path,
            arguments);
        std::vector<wchar_t> mutable_command(
            command_line.begin(),
            command_line.end());
        mutable_command.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(
                runtime_path.c_str(),
                mutable_command.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                runtime_path.parent_path().c_str(),
                &startup,
                &process) == FALSE) {
            std::cerr << "Unable to start contract-runtime\n";
            return static_cast<int>(
                contract::launcher::LauncherExitCode::invocation_failed);
        }

        const DWORD wait_result = WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exit_code = static_cast<DWORD>(
            contract::launcher::LauncherExitCode::invocation_failed);
        if (wait_result != WAIT_OBJECT_0 ||
            GetExitCodeProcess(process.hProcess, &exit_code) == FALSE) {
            std::cerr << "Unable to obtain the contract-runtime result\n";
            exit_code = static_cast<DWORD>(
                contract::launcher::LauncherExitCode::invocation_failed);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return static_cast<int>(exit_code);
    }
};
#else
class UnsupportedRuntimeProcess final : public contract::launcher::IRuntimeProcess {
public:
    int invoke(
        const std::filesystem::path&,
        const std::vector<std::filesystem::path>&,
        const std::optional<std::string>&) const override {
        std::cerr << "Runtime invocation is currently supported only on Windows\n";
        return static_cast<int>(
            contract::launcher::LauncherExitCode::invocation_failed);
    }
};
#endif

}

int main(int argc, char* argv[]) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    const auto options = contract::launcher::parse_launcher_options(
        arguments,
        std::cerr);
    if (!options.has_value()) {
        contract::launcher::print_launcher_help(std::cout);
        return static_cast<int>(
            contract::launcher::LauncherExitCode::usage_error);
    }
    if (options->help) {
        contract::launcher::print_launcher_help(std::cout);
        return static_cast<int>(
            contract::launcher::LauncherExitCode::success);
    }

    const contract::filesystem::NativeReadOnlyFilesystem filesystem;
#ifdef _WIN32
    const WindowsRuntimeProcess runtime_process;
#else
    const UnsupportedRuntimeProcess runtime_process;
#endif
    const contract::launcher::LauncherContext context{
        filesystem,
        contract::installation::default_recognition_policy(),
        contract::installation::environment_game_path(),
        contract::installation::configured_game_path(),
        contract::installation::default_windows_probe_paths(),
        runtime_process};
    return contract::launcher::run_launcher(
        *options,
        context,
        std::cout,
        std::cerr);
}
