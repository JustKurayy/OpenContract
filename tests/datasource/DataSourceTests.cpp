#include "TestSupport.hpp"

#include <contract/datasource/DataSource.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() /
                "contract-synthetic-datasource-test") {
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

}

int main() {
    using namespace contract::datasource;

    const std::array bytes{
        std::byte{0x10},
        std::byte{0x11},
        std::byte{0x12},
        std::byte{0x13},
        std::byte{0x14},
        std::byte{0x15},
        std::byte{0x16},
        std::byte{0x17}};
    MemoryDataSource source(bytes);
    CONTRACT_EXPECT_EQ(source.size(), std::uint64_t{8});

    ReadBudget budget(6, 4);
    const auto middle = source.read(2, 3, budget);
    CONTRACT_EXPECT(middle.has_value());
    CONTRACT_EXPECT_EQ(middle.value().size(), std::size_t{3});
    CONTRACT_EXPECT_EQ(std::to_integer<unsigned int>(middle.value()[0]), 0x12U);
    CONTRACT_EXPECT_EQ(budget.consumed(), std::uint64_t{3});
    CONTRACT_EXPECT_EQ(budget.remaining(), std::uint64_t{3});

    const auto per_read_limit = source.read(0, 5, budget);
    CONTRACT_EXPECT(!per_read_limit.has_value());
    CONTRACT_EXPECT_EQ(
        per_read_limit.error().code,
        DataSourceErrorCode::read_limit_exceeded);
    CONTRACT_EXPECT_EQ(budget.consumed(), std::uint64_t{3});

    const auto total_limit = source.read(0, 4, budget);
    CONTRACT_EXPECT(!total_limit.has_value());
    CONTRACT_EXPECT_EQ(
        total_limit.error().code,
        DataSourceErrorCode::total_budget_exceeded);
    CONTRACT_EXPECT_EQ(budget.consumed(), std::uint64_t{3});

    const auto end = source.read(8, 0, budget);
    CONTRACT_EXPECT(end.has_value());
    CONTRACT_EXPECT(end.value().empty());

    const auto beyond_end = source.read(8, 1, budget);
    CONTRACT_EXPECT(!beyond_end.has_value());
    CONTRACT_EXPECT_EQ(
        beyond_end.error().code,
        DataSourceErrorCode::end_of_source);

    const auto overflow = source.read(
        std::numeric_limits<std::uint64_t>::max(),
        2,
        budget);
    CONTRACT_EXPECT(!overflow.has_value());
    CONTRACT_EXPECT_EQ(
        overflow.error().code,
        DataSourceErrorCode::offset_overflow);

    TemporaryDirectory temporary;
    const auto file_path = temporary.path() / "synthetic-source.bin";
    {
        std::ofstream output(file_path, std::ios::binary);
        for (unsigned int value = 0; value < 16U; ++value) {
            output.put(static_cast<char>(value));
        }
    }

    const auto file_source = FileDataSource::open(file_path);
    CONTRACT_EXPECT(file_source.has_value());
    CONTRACT_EXPECT_EQ(file_source.value().size(), std::uint64_t{16});

    ReadBudget file_budget(4, 4);
    const auto file_middle = file_source.value().read(5, 4, file_budget);
    CONTRACT_EXPECT(file_middle.has_value());
    CONTRACT_EXPECT_EQ(file_middle.value().size(), std::size_t{4});
    CONTRACT_EXPECT_EQ(
        std::to_integer<unsigned int>(file_middle.value()[0]),
        5U);
    CONTRACT_EXPECT_EQ(
        std::to_integer<unsigned int>(file_middle.value()[3]),
        8U);
    CONTRACT_EXPECT_EQ(file_budget.consumed(), std::uint64_t{4});

    ReadBudget boundary_budget(8, 8);
    const auto file_end = file_source.value().read(15, 2, boundary_budget);
    CONTRACT_EXPECT(!file_end.has_value());
    CONTRACT_EXPECT_EQ(
        file_end.error().code,
        DataSourceErrorCode::end_of_source);
    CONTRACT_EXPECT_EQ(boundary_budget.consumed(), std::uint64_t{0});

    const auto missing_file = FileDataSource::open(temporary.path() / "missing.bin");
    CONTRACT_EXPECT(!missing_file.has_value());
    CONTRACT_EXPECT_EQ(
        missing_file.error().code,
        DataSourceErrorCode::path_missing);

    const auto directory_source = FileDataSource::open(temporary.path());
    CONTRACT_EXPECT(!directory_source.has_value());
    CONTRACT_EXPECT_EQ(
        directory_source.error().code,
        DataSourceErrorCode::not_a_file);

    return contract::test::finish();
}
