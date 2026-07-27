#pragma once

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace contract::tools {

struct ModValidatorOptions {
    std::optional<std::filesystem::path> manifest_path;
    bool json{false};
    bool check_assets{false};
    bool help{false};
};

enum class ModValidatorExitCode : int {
    success = 0,
    usage_error = 2,
    source_error = 3,
    invalid_manifest = 4,
    invalid_assets = 5
};

[[nodiscard]] std::optional<ModValidatorOptions> parse_mod_validator_options(
    const std::vector<std::string>& arguments,
    std::ostream& errors);

[[nodiscard]] int run_mod_validator(
    const ModValidatorOptions& options,
    std::ostream& output,
    std::ostream& errors);

void print_mod_validator_help(std::ostream& output);

}
