#include "TestSupport.hpp"

#include <contract/formats/ScenePlacementDecoder.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace {

void append_u8(
    std::vector<std::byte>& bytes,
    std::uint8_t value) {
    bytes.push_back(static_cast<std::byte>(value));
}

void append_u32(
    std::vector<std::byte>& bytes,
    std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        append_u8(
            bytes,
            static_cast<std::uint8_t>(
                (value >> (index * 8U)) & 0xffU));
    }
}

void append_f32(
    std::vector<std::byte>& bytes,
    float value) {
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
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

std::vector<std::byte> make_properties(
    std::uint32_t primitive_record,
    bool invisible = false) {
    std::vector<std::byte> bytes(31, std::byte{0});
    constexpr std::string_view magic{"IOPacked v0.1"};
    for (std::size_t index = 0; index < magic.size(); ++index) {
        bytes[index] = static_cast<std::byte>(magic[index]);
    }
    bytes[14] = std::byte{0};
    write_u32(bytes, 15, 13);
    write_u32(bytes, 23, 0);
    write_u32(bytes, 27, 1);

    append_u8(bytes, 0);
    append_u32(bytes, 1);

    append_u8(bytes, 0x04);
    append_u32(bytes, 1);
    append_u8(bytes, 0x0c);
    append_u32(bytes, 0);
    append_u8(bytes, 0x09);
    append_u32(bytes, 0x0c);
    append_u8(bytes, 0x0c);
    append_u32(bytes, 0);

    append_u8(bytes, 0x02);
    append_u8(bytes, 0x0e);
    append_u32(bytes, 0);
    append_u8(bytes, 0x01);
    append_u32(bytes, 9);
    const std::array<float, 9> matrix{
        0.0F, 1.0F, 0.0F,
        -1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F
    };
    for (const auto value : matrix) {
        append_u8(bytes, 0x0a);
        append_f32(bytes, value);
    }
    append_u8(bytes, 0x7c);
    append_u8(bytes, 0x01);
    append_u32(bytes, 3);
    for (const auto value : {10.0F, 20.0F, 30.0F}) {
        append_u8(bytes, 0x0a);
        append_f32(bytes, value);
    }
    append_u8(bytes, 0x7c);
    append_u8(bytes, 0x06);
    append_u8(bytes, 0);
    append_u8(bytes, 0x09);
    append_u32(bytes, primitive_record);
    if (invisible) {
        append_u8(bytes, 0x06);
        append_u8(bytes, 1);
    }
    append_u8(bytes, 0x7e);
    append_u8(bytes, 0x7f);
    return bytes;
}

}

int main() {
    const auto bytes = make_properties(42);
    const auto decoded =
        contract::formats::ScenePlacementDecoder::decode(bytes);
    CONTRACT_EXPECT(decoded.has_value());
    if (decoded.has_value()) {
        CONTRACT_EXPECT_EQ(decoded.value().declared_objects, std::uint32_t{1});
        CONTRACT_EXPECT_EQ(decoded.value().placements.size(), std::size_t{1});
        const auto& placement = decoded.value().placements.front();
        CONTRACT_EXPECT_EQ(
            placement.primitive_record,
            std::uint32_t{42});
        CONTRACT_EXPECT_EQ(placement.matrix[1], 1.0F);
        CONTRACT_EXPECT_EQ(placement.matrix[3], -1.0F);
        CONTRACT_EXPECT_EQ(placement.position[0], 10.0F);
        CONTRACT_EXPECT_EQ(placement.position[1], 20.0F);
        CONTRACT_EXPECT_EQ(placement.position[2], 30.0F);
        CONTRACT_EXPECT(!placement.inactive);
        CONTRACT_EXPECT(!placement.invisible);
    }

    const auto invisible_bytes = make_properties(7, true);
    const auto invisible =
        contract::formats::ScenePlacementDecoder::decode(
            invisible_bytes);
    CONTRACT_EXPECT(invisible.has_value());
    if (invisible.has_value()) {
        CONTRACT_EXPECT_EQ(
            invisible.value().placements.size(),
            std::size_t{1});
        CONTRACT_EXPECT(
            invisible.value().placements.front().invisible);
    }

    auto truncated_bytes = bytes;
    truncated_bytes.resize(truncated_bytes.size() - 5U);
    const auto truncated =
        contract::formats::ScenePlacementDecoder::decode(
            truncated_bytes);
    CONTRACT_EXPECT(!truncated.has_value());
    if (!truncated.has_value()) {
        CONTRACT_EXPECT_EQ(
            truncated.error().code,
            contract::formats::ScenePlacementErrorCode::truncated);
    }

    auto invalid_header = bytes;
    invalid_header[0] = std::byte{0};
    const auto invalid =
        contract::formats::ScenePlacementDecoder::decode(
            invalid_header);
    CONTRACT_EXPECT(!invalid.has_value());
    if (!invalid.has_value()) {
        CONTRACT_EXPECT_EQ(
            invalid.error().code,
            contract::formats::ScenePlacementErrorCode::invalid_header);
    }

    return contract::test::finish();
}
