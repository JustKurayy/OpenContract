#include "TestSupport.hpp"

#include <contract/formats/MaterialDatabaseDecoder.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

void write_u32(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::byte>(
            (value >> (index * 8U)) & 0xffU);
    }
}

void write_tag(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::string_view tag) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::byte>(
            tag[3U - index]);
    }
}

void write_node(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::string_view tag,
    std::uint32_t data,
    std::uint32_t size,
    std::uint32_t type) {
    write_tag(bytes, offset, tag);
    write_u32(bytes, offset + 4U, data);
    write_u32(bytes, offset + 8U, size);
    write_u32(bytes, offset + 12U, type);
}

void write_string(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::string_view value) {
    for (std::size_t index = 0; index < value.size(); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(value[index]);
    }
    bytes[offset + value.size()] = std::byte{0};
}

std::vector<std::byte> make_materials() {
    std::vector<std::byte> bytes(768, std::byte{0});
    write_u32(bytes, 4, 128);
    write_u32(bytes, 132, 192);
    write_u32(bytes, 136, 0);
    write_u32(bytes, 192, 400);
    write_u32(bytes, 220, 240);
    write_node(bytes, 240, "INST", 272, 3, 3);
    write_node(bytes, 272, "TEXT", 336, 2, 3);
    write_node(bytes, 288, "RSTA", 368, 6, 3);
    write_node(bytes, 304, "NAME", 650, 10, 1);
    write_node(bytes, 336, "NAME", 730, 10, 1);
    write_node(bytes, 352, "TXID", 123, 1, 2);
    write_node(bytes, 368, "BENA", 1, 1, 2);
    write_node(bytes, 384, "ATST", 1, 1, 2);
    write_node(bytes, 400, "AREF", 127, 1, 2);
    write_node(bytes, 416, "CULL", 690, 9, 1);
    write_node(bytes, 432, "BMOD", 710, 6, 1);
    write_node(
        bytes,
        448,
        "OPAC",
        std::bit_cast<std::uint32_t>(0.25F),
        1,
        0);
    write_string(bytes, 650, "Synthetic");
    write_string(bytes, 730, "mapDiffuse");
    write_string(bytes, 690, "TwoSided");
    write_string(bytes, 710, "TRANS");
    return bytes;
}

}

int main() {
    const auto decoded =
        contract::formats::MaterialDatabaseDecoder::decode(
            make_materials());
    CONTRACT_EXPECT(decoded.has_value());
    if (decoded.has_value()) {
        CONTRACT_EXPECT_EQ(
            decoded.value().materials.size(),
            std::size_t{1});
        const auto* material = decoded.value().find(1);
        CONTRACT_EXPECT(material != nullptr);
        if (material != nullptr) {
            CONTRACT_EXPECT(
                material->diffuse_texture_id.has_value());
            CONTRACT_EXPECT_EQ(
                material->diffuse_texture_id.value(),
                std::uint32_t{123});
            CONTRACT_EXPECT(material->blend_enabled);
            CONTRACT_EXPECT(material->alpha_test_enabled);
            CONTRACT_EXPECT_EQ(
                material->alpha_reference,
                std::uint8_t{127});
            CONTRACT_EXPECT_EQ(
                material->cull_mode,
                contract::formats::MaterialCullMode::two_sided);
            CONTRACT_EXPECT_EQ(
                material->blend_mode,
                contract::formats::MaterialBlendMode::alpha);
            CONTRACT_EXPECT_EQ(material->opacity, 0.25F);
            CONTRACT_EXPECT_EQ(
                material->name,
                std::string{"Synthetic"});
            CONTRACT_EXPECT(!material->collision_only);
            CONTRACT_EXPECT(!material->overlay_only);
        }
    }

    auto invalid = make_materials();
    write_u32(invalid, 220, 760);
    const auto failed =
        contract::formats::MaterialDatabaseDecoder::decode(invalid);
    CONTRACT_EXPECT(!failed.has_value());

    return contract::test::finish();
}
