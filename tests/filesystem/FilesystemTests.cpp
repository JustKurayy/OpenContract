#include "TestSupport.hpp"

#include <contract/filesystem/ReadOnlyFilesystem.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() / "contract-synthetic-filesystem-test") {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_ / "folder", error);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}

int main() {
    TemporaryDirectory temporary;
    {
        std::ofstream output(temporary.path() / "zeta.bin", std::ios::binary);
        output.put(static_cast<char>(0x12));
        output.put(static_cast<char>(0x34));
    }
    {
        std::ofstream output(temporary.path() / "alpha.bin", std::ios::binary);
        output.put(static_cast<char>(0x56));
    }

    contract::filesystem::NativeReadOnlyFilesystem filesystem;
    const auto normalized = filesystem.canonicalize(temporary.path() / "." / "folder" / "..");
    CONTRACT_EXPECT(normalized.has_value());
    CONTRACT_EXPECT_EQ(normalized.value(), std::filesystem::canonical(temporary.path()));

    const auto entries = filesystem.enumerate_top_level(temporary.path());
    CONTRACT_EXPECT(entries.has_value());
    CONTRACT_EXPECT_EQ(entries.value().size(), std::size_t{3});
    CONTRACT_EXPECT_EQ(entries.value()[0].name, std::string("alpha.bin"));
    CONTRACT_EXPECT_EQ(entries.value()[1].name, std::string("folder"));
    CONTRACT_EXPECT_EQ(entries.value()[2].name, std::string("zeta.bin"));

    const auto missing = filesystem.enumerate_top_level(temporary.path() / "missing");
    CONTRACT_EXPECT(!missing.has_value());
    CONTRACT_EXPECT_EQ(
        missing.error().code,
        contract::filesystem::FilesystemErrorCode::path_missing);

    const auto binary = filesystem.read_binary_file(temporary.path() / "zeta.bin", 2);
    CONTRACT_EXPECT(binary.has_value());
    CONTRACT_EXPECT_EQ(binary.value().size(), std::size_t{2});

    const auto limited = filesystem.read_binary_file(temporary.path() / "zeta.bin", 1);
    CONTRACT_EXPECT(!limited.has_value());
    CONTRACT_EXPECT_EQ(
        limited.error().code,
        contract::filesystem::FilesystemErrorCode::size_limit_exceeded);

    const auto view = binary.value().view(1, 1);
    CONTRACT_EXPECT(view.has_value());
    CONTRACT_EXPECT_EQ(std::to_integer<unsigned int>(view.value()[0]), 0x34U);
    CONTRACT_EXPECT(!binary.value().view(2, 1).has_value());

    return contract::test::finish();
}
