#include "TestSupport.hpp"

#include <contract/binaryio/SourceBinaryReader.hpp>

#include <contract/core/Result.hpp>
#include <contract/datasource/DataSource.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

class HugeSyntheticSource final
    : public contract::datasource::IReadOnlyDataSource {
public:
    std::uint64_t size() const noexcept override {
        return std::numeric_limits<std::uint64_t>::max();
    }

    contract::core::Result<
        std::vector<std::byte>,
        contract::datasource::DataSourceError>
    read(
        std::uint64_t offset,
        std::size_t,
        contract::datasource::ReadBudget&) const override {
        return contract::core::Result<
            std::vector<std::byte>,
            contract::datasource::DataSourceError>::failure(
            {
                contract::datasource::DataSourceErrorCode::io_error,
                offset,
                "Synthetic source does not provide bytes"
            });
    }
};

}

int main() {
    using namespace contract;

    const std::array bytes{
        std::byte{0x34},
        std::byte{0x12},
        std::byte{0x78},
        std::byte{0x56},
        std::byte{0x34},
        std::byte{0x12},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x80},
        std::byte{0x3f}};
    datasource::MemoryDataSource source(bytes);
    datasource::ReadBudget budget(10, 8);
    binaryio::SourceBinaryReader reader(source, budget);

    const auto u16 = reader.read_u16();
    CONTRACT_EXPECT(u16.has_value());
    CONTRACT_EXPECT_EQ(u16.value(), std::uint16_t{0x1234});

    const auto u32 = reader.read_u32();
    CONTRACT_EXPECT(u32.has_value());
    CONTRACT_EXPECT_EQ(u32.value(), std::uint32_t{0x12345678});

    const auto floating = reader.read_f32();
    CONTRACT_EXPECT(floating.has_value());
    CONTRACT_EXPECT_EQ(floating.value(), 1.0F);
    CONTRACT_EXPECT_EQ(reader.offset(), std::uint64_t{10});
    CONTRACT_EXPECT_EQ(reader.remaining(), std::uint64_t{0});
    CONTRACT_EXPECT_EQ(budget.consumed(), std::uint64_t{10});

    const auto truncated = reader.read_u8();
    CONTRACT_EXPECT(!truncated.has_value());
    CONTRACT_EXPECT_EQ(
        truncated.error().code,
        binaryio::SourceBinaryErrorCode::end_of_source);
    CONTRACT_EXPECT_EQ(truncated.error().offset, std::uint64_t{10});

    datasource::ReadBudget limited_budget(1, 1);
    binaryio::SourceBinaryReader limited(source, limited_budget);
    const auto budget_failure = limited.read_u16();
    CONTRACT_EXPECT(!budget_failure.has_value());
    CONTRACT_EXPECT_EQ(
        budget_failure.error().code,
        binaryio::SourceBinaryErrorCode::source_error);
    CONTRACT_EXPECT(budget_failure.error().source_error.has_value());
    CONTRACT_EXPECT_EQ(
        budget_failure.error().source_error->code,
        datasource::DataSourceErrorCode::read_limit_exceeded);
    CONTRACT_EXPECT_EQ(limited.offset(), std::uint64_t{0});

    datasource::ReadBudget seek_budget(0, 0);
    binaryio::SourceBinaryReader seeking(source, seek_budget);
    CONTRACT_EXPECT(seeking.seek(10).has_value());
    const auto past_end = seeking.seek(11);
    CONTRACT_EXPECT(!past_end.has_value());
    CONTRACT_EXPECT_EQ(
        past_end.error().code,
        binaryio::SourceBinaryErrorCode::end_of_source);
    const auto invalid_alignment = seeking.align(3);
    CONTRACT_EXPECT(!invalid_alignment.has_value());
    CONTRACT_EXPECT_EQ(
        invalid_alignment.error().code,
        binaryio::SourceBinaryErrorCode::invalid_alignment);

    HugeSyntheticSource huge_source;
    datasource::ReadBudget huge_budget(0, 0);
    binaryio::SourceBinaryReader huge_reader(huge_source, huge_budget);
    CONTRACT_EXPECT(huge_reader.seek(
        std::numeric_limits<std::uint64_t>::max() - 1).has_value());
    const auto alignment_overflow = huge_reader.align(8);
    CONTRACT_EXPECT(!alignment_overflow.has_value());
    CONTRACT_EXPECT_EQ(
        alignment_overflow.error().code,
        binaryio::SourceBinaryErrorCode::overflow);

    return test::finish();
}
