#include "TestSupport.hpp"

#include <contract/datasource/DataSource.hpp>
#include <contract/formats/PrimitiveContainer.hpp>
#include <contract/formats/PrimitiveRigDecoder.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace {

void write_u32(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(
                (value >> (index * 8U)) & 0xffU);
    }
}

void write_i32(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::int32_t value) {
    write_u32(
        bytes,
        offset,
        static_cast<std::uint32_t>(value));
}

void write_f32(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    float value) {
    write_u32(
        bytes,
        offset,
        std::bit_cast<std::uint32_t>(value));
}

void write_name(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::string_view name) {
    for (std::size_t index = 0; index < name.size(); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(name[index]);
    }
}

std::vector<std::byte> make_rig_container(
    std::int32_t child_parent = 0,
    std::size_t bone_record_size = 128) {
    constexpr std::size_t record_count = 6;
    const std::vector<std::size_t> sizes{
        16,
        bone_record_size,
        96,
        80,
        16,
        64
    };
    std::vector<std::uint32_t> offsets;
    std::uint32_t cursor = 0;
    for (const auto size : sizes) {
        offsets.push_back(cursor);
        cursor += static_cast<std::uint32_t>(size);
    }
    const auto directory_offset = cursor;
    std::vector<std::byte> bytes(
        directory_offset + record_count * 16U,
        std::byte{0});

    write_u32(bytes, 0, directory_offset);
    write_u32(bytes, 4, record_count);
    write_u32(bytes, 8, directory_offset);

    if (bone_record_size >= 64U) {
        write_i32(bytes, offsets[1] + 12U, -1);
        write_name(bytes, offsets[1] + 28U, "ROOT");
    }
    if (bone_record_size >= 128U) {
        write_i32(
            bytes,
            offsets[1] + 64U + 12U,
            child_parent);
        write_name(
            bytes,
            offsets[1] + 64U + 28U,
            "SPINE");
    }

    write_f32(bytes, offsets[2] + 36U, 0.0F);
    write_f32(bytes, offsets[2] + 40U, 0.0F);
    write_f32(bytes, offsets[2] + 44U, 0.0F);
    write_f32(bytes, offsets[2], 1.0F);
    write_f32(bytes, offsets[2] + 16U, 1.0F);
    write_f32(bytes, offsets[2] + 32U, 1.0F);
    write_f32(bytes, offsets[2] + 48U, 0.0F);
    write_f32(bytes, offsets[2] + 48U + 4U, -1.0F);
    write_f32(bytes, offsets[2] + 48U + 8U, 0.0F);
    write_f32(bytes, offsets[2] + 48U + 12U, 1.0F);
    write_f32(bytes, offsets[2] + 48U + 16U, 0.0F);
    write_f32(bytes, offsets[2] + 48U + 20U, 0.0F);
    write_f32(bytes, offsets[2] + 48U + 24U, 0.0F);
    write_f32(bytes, offsets[2] + 48U + 28U, 0.0F);
    write_f32(bytes, offsets[2] + 48U + 32U, 1.0F);
    write_f32(bytes, offsets[2] + 48U + 36U, 0.0F);
    write_f32(bytes, offsets[2] + 48U + 40U, 100.0F);
    write_f32(bytes, offsets[2] + 48U + 44U, 5.0F);

    write_u32(bytes, offsets[3], 2);
    write_u32(bytes, offsets[3] + 4U, 1);
    write_u32(bytes, offsets[3] + 8U, 2);
    write_u32(bytes, offsets[4] + 4U, 3);
    write_u32(bytes, offsets[5] + 16U, 4);

    for (std::size_t index = 0; index < record_count; ++index) {
        const auto descriptor =
            directory_offset + index * 16U;
        write_u32(bytes, descriptor, offsets[index]);
        write_u32(
            bytes,
            descriptor + 4U,
            static_cast<std::uint32_t>(sizes[index]));
        write_u32(bytes, descriptor + 8U, 1);
    }
    return bytes;
}

}

int main() {
    const auto bytes = make_rig_container();
    contract::datasource::MemoryDataSource source(bytes);
    contract::datasource::ReadBudget index_budget(4096, 64);
    const auto container =
        contract::formats::PrimitiveContainerIndex::read(
            source,
            index_budget);
    CONTRACT_EXPECT(container.has_value());
    if (container.has_value()) {
        contract::datasource::ReadBudget decode_budget(4096, 256);
        const auto rig =
            contract::formats::PrimitiveRigDecoder::decode(
                container.value(),
                source,
                5,
                decode_budget);
        CONTRACT_EXPECT(rig.has_value());
        if (rig.has_value()) {
            CONTRACT_EXPECT_EQ(
                rig.value().model_record,
                std::uint32_t{5});
            CONTRACT_EXPECT_EQ(
                rig.value().bones.size(),
                std::size_t{2});
            CONTRACT_EXPECT_EQ(
                rig.value().bones[0].name,
                std::string{"ROOT"});
            CONTRACT_EXPECT(
                !rig.value().bones[0].parent_index.has_value());
            CONTRACT_EXPECT_EQ(
                rig.value().bones[1].name,
                std::string{"SPINE"});
            CONTRACT_EXPECT_EQ(
                rig.value().bones[1].parent_index.value(),
                std::size_t{0});
            CONTRACT_EXPECT_EQ(
                rig.value().bones[1].reference_position[0],
                0.0F);
            CONTRACT_EXPECT_EQ(
                rig.value().bones[1].reference_position[1],
                100.0F);
            CONTRACT_EXPECT_EQ(
                rig.value().bones[1].reference_position[2],
                5.0F);
            CONTRACT_EXPECT_EQ(
                rig.value().bones[0].reference_basis[0],
                1.0F);
            CONTRACT_EXPECT_EQ(
                rig.value().bones[0].reference_basis[4],
                1.0F);
            CONTRACT_EXPECT_EQ(
                rig.value().bones[0].reference_basis[8],
                1.0F);
            CONTRACT_EXPECT_EQ(
                rig.value().bones[1].reference_basis[1],
                -1.0F);
            CONTRACT_EXPECT_EQ(
                rig.value().bones[1].reference_basis[3],
                1.0F);
            CONTRACT_EXPECT_EQ(
                rig.value().bones[1].reference_basis[8],
                1.0F);
        }
    }

    const auto invalid_parent_bytes =
        make_rig_container(1);
    contract::datasource::MemoryDataSource invalid_parent_source(
        invalid_parent_bytes);
    contract::datasource::ReadBudget invalid_index_budget(
        4096,
        64);
    const auto invalid_parent_container =
        contract::formats::PrimitiveContainerIndex::read(
            invalid_parent_source,
            invalid_index_budget);
    CONTRACT_EXPECT(invalid_parent_container.has_value());
    if (invalid_parent_container.has_value()) {
        contract::datasource::ReadBudget decode_budget(4096, 256);
        const auto invalid =
            contract::formats::PrimitiveRigDecoder::decode(
                invalid_parent_container.value(),
                invalid_parent_source,
                5,
                decode_budget);
        CONTRACT_EXPECT(!invalid.has_value());
        if (!invalid.has_value()) {
            CONTRACT_EXPECT_EQ(
                invalid.error().code,
                contract::formats::PrimitiveRigDecodeErrorCode::
                    invalid_hierarchy);
        }
    }

    auto non_finite_basis_bytes = make_rig_container();
    write_f32(
        non_finite_basis_bytes,
        16U + 128U,
        std::numeric_limits<float>::quiet_NaN());
    contract::datasource::MemoryDataSource non_finite_basis_source(
        non_finite_basis_bytes);
    contract::datasource::ReadBudget non_finite_index_budget(
        4096,
        64);
    const auto non_finite_basis_container =
        contract::formats::PrimitiveContainerIndex::read(
            non_finite_basis_source,
            non_finite_index_budget);
    CONTRACT_EXPECT(non_finite_basis_container.has_value());
    if (non_finite_basis_container.has_value()) {
        contract::datasource::ReadBudget decode_budget(4096, 256);
        const auto invalid =
            contract::formats::PrimitiveRigDecoder::decode(
                non_finite_basis_container.value(),
                non_finite_basis_source,
                5,
                decode_budget);
        CONTRACT_EXPECT(!invalid.has_value());
        if (!invalid.has_value()) {
            CONTRACT_EXPECT_EQ(
                invalid.error().code,
                contract::formats::PrimitiveRigDecodeErrorCode::
                    unsupported_layout);
        }
    }

    const auto short_bones = make_rig_container(0, 64);
    contract::datasource::MemoryDataSource short_source(short_bones);
    contract::datasource::ReadBudget short_index_budget(4096, 64);
    const auto short_container =
        contract::formats::PrimitiveContainerIndex::read(
            short_source,
            short_index_budget);
    CONTRACT_EXPECT(short_container.has_value());
    if (short_container.has_value()) {
        contract::datasource::ReadBudget decode_budget(4096, 256);
        const auto invalid =
            contract::formats::PrimitiveRigDecoder::decode(
                short_container.value(),
                short_source,
                5,
                decode_budget);
        CONTRACT_EXPECT(!invalid.has_value());
        if (!invalid.has_value()) {
            CONTRACT_EXPECT_EQ(
                invalid.error().code,
                contract::formats::PrimitiveRigDecodeErrorCode::
                    unsupported_layout);
        }
    }

    return contract::test::finish();
}
