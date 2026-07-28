#include "TestSupport.hpp"

#include <contract/formats/TextureDatabaseDecoder.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
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
        bytes[offset + index] = static_cast<std::byte>(
            (value >> (index * 8U)) & 0xffU);
    }
}

void write_type(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::string_view value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(value[3U - index]);
    }
}

std::vector<std::byte> make_textures() {
    constexpr std::uint32_t table_offset = 16;
    constexpr std::uint32_t record_offset =
        table_offset + 2048U * 4U;
    std::vector<std::byte> bytes(
        record_offset + 50U,
        std::byte{0});
    write_u32(bytes, 0, table_offset);
    write_u32(bytes, table_offset, record_offset);
    write_type(bytes, record_offset + 4U, "DXT1");
    write_type(bytes, record_offset + 8U, "DXT1");
    write_u32(bytes, record_offset + 12U, 123);
    write_u16(bytes, record_offset + 16U, 4);
    write_u16(bytes, record_offset + 18U, 4);
    write_u32(bytes, record_offset + 20U, 1);
    bytes[record_offset + 36U] = std::byte{'x'};
    bytes[record_offset + 37U] = std::byte{0};
    write_u32(bytes, record_offset + 38U, 8);
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[record_offset + 42U + index] =
            static_cast<std::byte>(index);
    }
    return bytes;
}

}

int main() {
    const auto bytes = make_textures();
    const auto decoded =
        contract::formats::TextureDatabaseDecoder::decode(bytes);
    CONTRACT_EXPECT(decoded.has_value());
    if (decoded.has_value()) {
        const auto* texture = decoded.value().find(123);
        CONTRACT_EXPECT(texture != nullptr);
        if (texture != nullptr) {
            CONTRACT_EXPECT_EQ(
                texture->format,
                contract::formats::TextureFormat::bc1);
            CONTRACT_EXPECT_EQ(texture->width, std::uint16_t{4});
            CONTRACT_EXPECT_EQ(texture->height, std::uint16_t{4});
            const auto mip = texture->first_mip(bytes);
            CONTRACT_EXPECT(mip.has_value());
            if (mip.has_value()) {
                CONTRACT_EXPECT_EQ(mip.value().size(), std::size_t{8});
                CONTRACT_EXPECT_EQ(
                    mip.value()[7],
                    std::byte{7});
            }
        }
    }

    auto invalid = bytes;
    write_u32(invalid, 16, 0xffff'fff0U);
    const auto failed =
        contract::formats::TextureDatabaseDecoder::decode(invalid);
    CONTRACT_EXPECT(!failed.has_value());

    return contract::test::finish();
}
