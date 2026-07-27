#include "TestSupport.hpp"

#include <contract/tools/ModValidator.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() /
                "contract-synthetic-mod-validator-test") {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_, error);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

const char* valid_manifest =
    "{"
    "\"schema_version\":1,"
    "\"package\":{"
    "\"id\":\"package.synthetic\","
    "\"version\":{\"major\":1,\"minor\":0,\"patch\":0},"
    "\"metadata\":{"
    "\"name\":\"Synthetic\","
    "\"author\":\"Synthetic Author\","
    "\"description\":\"Synthetic only\""
    "}},"
    "\"dependencies\":[],"
    "\"assets\":[],"
    "\"navigation_graphs\":[],"
    "\"maps\":[],"
    "\"missions\":[]"
    "}";

const char* asset_manifest =
    "{"
    "\"schema_version\":1,"
    "\"package\":{"
    "\"id\":\"package.assets\","
    "\"version\":{\"major\":1,\"minor\":0,\"patch\":0},"
    "\"metadata\":{"
    "\"name\":\"Synthetic\","
    "\"author\":\"Synthetic Author\","
    "\"description\":\"Synthetic only\""
    "}},"
    "\"dependencies\":[],"
    "\"assets\":[{\"id\":\"asset.synthetic\",\"source\":\"assets/item.bin\"}],"
    "\"navigation_graphs\":[],"
    "\"maps\":[],"
    "\"missions\":[]"
    "}";

}

int main() {
    using contract::tools::ModValidatorExitCode;

    std::ostringstream parse_errors;
    const auto parsed = contract::tools::parse_mod_validator_options(
        {
            "--manifest",
            "C:/Synthetic Package/manifest.json",
            "--json",
            "--check-assets"
        },
        parse_errors);
    CONTRACT_EXPECT(parsed.has_value());
    CONTRACT_EXPECT(parsed->json);
    CONTRACT_EXPECT(parsed->check_assets);
    CONTRACT_EXPECT_EQ(
        parsed->manifest_path.value(),
        std::filesystem::path("C:/Synthetic Package/manifest.json"));
    CONTRACT_EXPECT(parse_errors.str().empty());

    TemporaryDirectory temporary;
    const auto valid_path = temporary.path() / "valid.contract.json";
    {
        std::ofstream output(valid_path, std::ios::binary);
        output << valid_manifest;
    }

    std::ostringstream output;
    std::ostringstream errors;
    const auto valid_exit = contract::tools::run_mod_validator(
        {valid_path, true, false, false},
        output,
        errors);
    CONTRACT_EXPECT_EQ(
        valid_exit,
        static_cast<int>(ModValidatorExitCode::success));
    CONTRACT_EXPECT(errors.str().empty());
    CONTRACT_EXPECT(
        output.str().find("\"package_id\":\"package.synthetic\"") !=
        std::string::npos);

    const auto invalid_path = temporary.path() / "invalid.contract.json";
    {
        std::ofstream output_file(invalid_path, std::ios::binary);
        output_file << "{";
    }
    std::ostringstream invalid_output;
    std::ostringstream invalid_errors;
    const auto invalid_exit = contract::tools::run_mod_validator(
        {invalid_path, false, false, false},
        invalid_output,
        invalid_errors);
    CONTRACT_EXPECT_EQ(
        invalid_exit,
        static_cast<int>(ModValidatorExitCode::invalid_manifest));
    CONTRACT_EXPECT(!invalid_errors.str().empty());

    std::ostringstream missing_output;
    std::ostringstream missing_errors;
    const auto missing_exit = contract::tools::run_mod_validator(
        {temporary.path() / "missing.json", false, false, false},
        missing_output,
        missing_errors);
    CONTRACT_EXPECT_EQ(
        missing_exit,
        static_cast<int>(ModValidatorExitCode::source_error));

    const auto asset_path = temporary.path() / "assets.contract.json";
    {
        std::ofstream asset_output(asset_path, std::ios::binary);
        asset_output << asset_manifest;
    }
    std::ostringstream asset_output;
    std::ostringstream asset_errors;
    const auto asset_exit = contract::tools::run_mod_validator(
        {asset_path, false, true, false},
        asset_output,
        asset_errors);
    CONTRACT_EXPECT_EQ(
        asset_exit,
        static_cast<int>(ModValidatorExitCode::invalid_assets));
    CONTRACT_EXPECT(
        asset_errors.str().find("asset.synthetic") != std::string::npos);

    std::error_code directory_error;
    std::filesystem::create_directories(
        temporary.path() / "assets",
        directory_error);
    {
        std::ofstream source_output(
            temporary.path() / "assets" / "item.bin",
            std::ios::binary);
        source_output << "synthetic";
    }
    std::ostringstream complete_output;
    std::ostringstream complete_errors;
    const auto complete_exit = contract::tools::run_mod_validator(
        {asset_path, true, true, false},
        complete_output,
        complete_errors);
    CONTRACT_EXPECT_EQ(
        complete_exit,
        static_cast<int>(ModValidatorExitCode::success));
    CONTRACT_EXPECT(complete_errors.str().empty());
    CONTRACT_EXPECT(
        complete_output.str().find("\"assets_checked\":true") !=
        std::string::npos);

    return contract::test::finish();
}
