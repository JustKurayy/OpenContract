#include "TestSupport.hpp"

#include <contract/datasource/DataSource.hpp>
#include <contract/formats/PrimitiveContainer.hpp>
#include <contract/formats/PrimitiveSceneDecoder.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

void write_u16(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] =
        static_cast<std::byte>((value >> 8U) & 0xffU);
}

void write_u32(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

void write_f32(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    float value) {
    write_u32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

std::vector<std::byte> make_scene_container(
    bool invalid_index,
    bool primary_lod = true) {
    const std::vector<std::size_t> sizes{
        16,
        16,
        120,
        16,
        16,
        64,
        16,
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
        directory_offset + sizes.size() * 16U,
        std::byte{0});

    write_u32(bytes, 0, directory_offset);
    write_u32(bytes, 4, static_cast<std::uint32_t>(sizes.size()));
    write_u32(bytes, 8, directory_offset);

    write_u16(bytes, offsets[1], 1);
    write_u16(bytes, offsets[1] + 2U, 3);
    write_u16(bytes, offsets[1] + 4U, 0);
    write_u16(bytes, offsets[1] + 6U, 1);
    write_u16(bytes, offsets[1] + 8U, invalid_index ? 3 : 2);

    const float positions[9]{
        0.0F, 0.0F, 0.0F,
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F
    };
    for (std::size_t vertex = 0; vertex < 3; ++vertex) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            write_f32(
                bytes,
                offsets[2] + vertex * 40U + axis * 4U,
                positions[vertex * 3U + axis]);
        }
        write_f32(
            bytes,
            offsets[2] + vertex * 40U + 20U,
            static_cast<float>(vertex) * 0.25F);
        write_f32(
            bytes,
            offsets[2] + vertex * 40U + 24U,
            static_cast<float>(vertex) * 0.5F);
    }

    write_u32(bytes, offsets[3], 3);
    write_u32(bytes, offsets[3] + 4U, 2);
    write_u32(bytes, offsets[3] + 12U, 1);
    write_u32(bytes, offsets[4], 3);
    bytes[offsets[5] + 14U] =
        primary_lod ? std::byte{1} : std::byte{0};
    write_u16(bytes, offsets[5] + 18U, 77);
    write_u32(bytes, offsets[5] + 40U, 4);
    write_u32(bytes, offsets[6], 5);

    write_u32(bytes, offsets[8] + 16U, 7);
    write_u32(bytes, offsets[8] + 20U, 1);
    write_u32(bytes, offsets[8] + 24U, 6);
    write_f32(bytes, offsets[8] + 32U, 0.0F);
    write_f32(bytes, offsets[8] + 36U, 0.0F);
    write_f32(bytes, offsets[8] + 40U, 0.0F);
    write_f32(bytes, offsets[8] + 44U, 1.0F);
    write_f32(bytes, offsets[8] + 48U, 1.0F);
    write_f32(bytes, offsets[8] + 52U, 0.0F);

    for (std::size_t index = 0; index < sizes.size(); ++index) {
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
    const auto scene_bytes = make_scene_container(false);
    contract::datasource::MemoryDataSource scene_source(scene_bytes);
    contract::datasource::ReadBudget index_budget(4096, 256);
    const auto container =
        contract::formats::PrimitiveContainerIndex::read(
            scene_source,
            index_budget);
    CONTRACT_EXPECT(container.has_value());
    if (container.has_value()) {
        contract::datasource::ReadBudget decode_budget(4096, 256);
        const auto scene =
            contract::formats::PrimitiveSceneDecoder::decode(
                container.value(),
                scene_source,
                decode_budget);
        CONTRACT_EXPECT(scene.has_value());
        if (scene.has_value()) {
            CONTRACT_EXPECT_EQ(scene.value().meshes.size(), std::size_t{1});
            CONTRACT_EXPECT_EQ(
                scene.value().candidate_models,
                std::size_t{1});
            CONTRACT_EXPECT_EQ(
                scene.value().meshes[0].vertex_stride,
                std::uint32_t{40});
            CONTRACT_EXPECT_EQ(
                scene.value().meshes[0].material_id,
                std::uint16_t{77});
            CONTRACT_EXPECT_EQ(
                scene.value().meshes[0].positions.size(),
                std::size_t{3});
            CONTRACT_EXPECT_EQ(
                scene.value().meshes[0].indices.size(),
                std::size_t{3});
            CONTRACT_EXPECT_EQ(
                scene.value().meshes[0].positions[1].x,
                1.0F);
            CONTRACT_EXPECT_EQ(
                scene.value().meshes[0].texture_coordinates[2].u,
                0.5F);
            CONTRACT_EXPECT_EQ(
                scene.value().meshes[0].texture_coordinates[2].v,
                1.0F);
            CONTRACT_EXPECT_EQ(
                scene.value().meshes[0].indices[2],
                std::uint32_t{2});
        }
    }

    const auto invalid_bytes = make_scene_container(true);
    contract::datasource::MemoryDataSource invalid_source(invalid_bytes);
    contract::datasource::ReadBudget invalid_index_budget(4096, 256);
    const auto invalid_container =
        contract::formats::PrimitiveContainerIndex::read(
            invalid_source,
            invalid_index_budget);
    CONTRACT_EXPECT(invalid_container.has_value());
    if (invalid_container.has_value()) {
        contract::datasource::ReadBudget invalid_decode_budget(4096, 256);
        const auto invalid =
            contract::formats::PrimitiveSceneDecoder::decode(
                invalid_container.value(),
                invalid_source,
                invalid_decode_budget);
        CONTRACT_EXPECT(!invalid.has_value());
        if (!invalid.has_value()) {
            CONTRACT_EXPECT_EQ(
                invalid.error().code,
                contract::formats::PrimitiveSceneDecodeErrorCode::no_meshes);
        }
    }

    const auto secondary_lod_bytes =
        make_scene_container(false, false);
    contract::datasource::MemoryDataSource secondary_lod_source(
        secondary_lod_bytes);
    contract::datasource::ReadBudget secondary_lod_index_budget(
        4096,
        256);
    const auto secondary_lod_container =
        contract::formats::PrimitiveContainerIndex::read(
            secondary_lod_source,
            secondary_lod_index_budget);
    CONTRACT_EXPECT(secondary_lod_container.has_value());
    if (secondary_lod_container.has_value()) {
        contract::datasource::ReadBudget secondary_lod_decode_budget(
            4096,
            256);
        const auto secondary_lod =
            contract::formats::PrimitiveSceneDecoder::decode(
                secondary_lod_container.value(),
                secondary_lod_source,
                secondary_lod_decode_budget);
        CONTRACT_EXPECT(!secondary_lod.has_value());
    }

    return contract::test::finish();
}
