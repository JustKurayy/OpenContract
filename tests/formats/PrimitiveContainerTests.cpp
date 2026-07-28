#include "TestSupport.hpp"

#include <contract/datasource/DataSource.hpp>
#include <contract/formats/PrimitiveContainer.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

void write_u32(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

std::vector<std::byte> make_container() {
    std::vector<std::byte> bytes(64, std::byte{0});
    write_u32(bytes, 0, 32);
    write_u32(bytes, 4, 2);
    write_u32(bytes, 8, 32);
    write_u32(bytes, 12, 0);
    for (std::size_t index = 16; index < 32; ++index) {
        bytes[index] = static_cast<std::byte>(index);
    }

    write_u32(bytes, 32, 0);
    write_u32(bytes, 36, 16);
    write_u32(bytes, 40, 1);
    write_u32(bytes, 44, 0);
    write_u32(bytes, 48, 16);
    write_u32(bytes, 52, 16);
    write_u32(bytes, 56, 7);
    write_u32(bytes, 60, 42);
    return bytes;
}

}

int main() {
    using contract::formats::PrimitiveContainerErrorCode;
    using contract::formats::PrimitiveContainerIndex;

    const auto valid_bytes = make_container();
    contract::datasource::MemoryDataSource valid_source(valid_bytes);
    contract::datasource::ReadBudget valid_budget(1024, 64);
    const auto valid = PrimitiveContainerIndex::read(
        valid_source,
        valid_budget);
    CONTRACT_EXPECT(valid.has_value());
    if (valid.has_value()) {
        CONTRACT_EXPECT_EQ(
            valid.value().header().directory_offset,
            std::uint64_t{32});
        CONTRACT_EXPECT_EQ(
            valid.value().header().record_count,
            std::uint32_t{2});
        CONTRACT_EXPECT_EQ(
            valid.value().records()[1].kind,
            std::uint32_t{7});
        CONTRACT_EXPECT_EQ(
            valid.value().records()[1].metadata,
            std::uint32_t{42});

        const auto record =
            valid.value().read_record(valid_source, 1, valid_budget);
        CONTRACT_EXPECT(record.has_value());
        if (record.has_value()) {
            CONTRACT_EXPECT_EQ(record.value().size(), std::size_t{16});
            CONTRACT_EXPECT_EQ(record.value()[0], std::byte{16});
            CONTRACT_EXPECT_EQ(record.value()[15], std::byte{31});
        }
        const auto missing =
            valid.value().read_record(valid_source, 2, valid_budget);
        CONTRACT_EXPECT(!missing.has_value());
        if (!missing.has_value()) {
            CONTRACT_EXPECT_EQ(
                missing.error().code,
                PrimitiveContainerErrorCode::invalid_record);
        }
    }

    auto mismatched_offsets = make_container();
    write_u32(mismatched_offsets, 8, 48);
    contract::datasource::MemoryDataSource mismatch_source(
        mismatched_offsets);
    contract::datasource::ReadBudget mismatch_budget(256, 64);
    const auto mismatch =
        PrimitiveContainerIndex::read(mismatch_source, mismatch_budget);
    CONTRACT_EXPECT(!mismatch.has_value());
    if (!mismatch.has_value()) {
        CONTRACT_EXPECT_EQ(
            mismatch.error().code,
            PrimitiveContainerErrorCode::invalid_header);
    }

    auto outside_record = make_container();
    write_u32(outside_record, 48, 24);
    write_u32(outside_record, 52, 16);
    contract::datasource::MemoryDataSource outside_source(outside_record);
    contract::datasource::ReadBudget outside_budget(256, 64);
    const auto outside =
        PrimitiveContainerIndex::read(outside_source, outside_budget);
    CONTRACT_EXPECT(!outside.has_value());
    if (!outside.has_value()) {
        CONTRACT_EXPECT_EQ(
            outside.error().code,
            PrimitiveContainerErrorCode::invalid_record);
    }

    auto too_many_records = make_container();
    write_u32(too_many_records, 4, 3);
    contract::datasource::MemoryDataSource count_source(too_many_records);
    contract::datasource::ReadBudget count_budget(256, 64);
    const auto count = PrimitiveContainerIndex::read(
        count_source,
        count_budget,
        {.max_records = 2, .max_record_size = 1024});
    CONTRACT_EXPECT(!count.has_value());
    if (!count.has_value()) {
        CONTRACT_EXPECT_EQ(
            count.error().code,
            PrimitiveContainerErrorCode::record_limit_exceeded);
    }

    std::vector<std::byte> truncated_bytes(15, std::byte{0});
    contract::datasource::MemoryDataSource truncated_source(
        truncated_bytes);
    contract::datasource::ReadBudget truncated_budget(64, 64);
    const auto truncated =
        PrimitiveContainerIndex::read(truncated_source, truncated_budget);
    CONTRACT_EXPECT(!truncated.has_value());
    if (!truncated.has_value()) {
        CONTRACT_EXPECT_EQ(
            truncated.error().code,
            PrimitiveContainerErrorCode::truncated);
    }

    return contract::test::finish();
}
