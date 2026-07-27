#include "TestSupport.hpp"

#include <contract/binaryio/BinaryReader.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

int main() {
    using contract::binaryio::BinaryErrorCode;
    using contract::binaryio::BinaryReader;

    const std::array bytes{
        std::byte{0x78},
        std::byte{0x56},
        std::byte{0x34},
        std::byte{0x12},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x80},
        std::byte{0x3f}};

    BinaryReader reader(bytes);
    const auto integer = reader.read_u32();
    CONTRACT_EXPECT(integer.has_value());
    CONTRACT_EXPECT_EQ(integer.value(), std::uint32_t{0x12345678});

    const auto floating = reader.read_f32();
    CONTRACT_EXPECT(floating.has_value());
    CONTRACT_EXPECT_EQ(floating.value(), 1.0F);
    CONTRACT_EXPECT_EQ(reader.offset(), std::size_t{8});

    const auto truncated = reader.read_u8();
    CONTRACT_EXPECT(!truncated.has_value());
    CONTRACT_EXPECT_EQ(truncated.error().code, BinaryErrorCode::end_of_file);
    CONTRACT_EXPECT_EQ(truncated.error().offset, std::size_t{8});

    BinaryReader oversized(bytes);
    CONTRACT_EXPECT(oversized.seek(1).has_value());
    const auto too_many = oversized.read_bytes(std::numeric_limits<std::size_t>::max());
    CONTRACT_EXPECT(!too_many.has_value());
    CONTRACT_EXPECT_EQ(too_many.error().code, BinaryErrorCode::overflow);

    BinaryReader seeking(bytes);
    CONTRACT_EXPECT(!seeking.seek(9).has_value());
    CONTRACT_EXPECT_EQ(seeking.seek(9).error().code, BinaryErrorCode::end_of_file);
    CONTRACT_EXPECT(!seeking.align(0).has_value());
    CONTRACT_EXPECT_EQ(seeking.align(0).error().code, BinaryErrorCode::invalid_alignment);
    CONTRACT_EXPECT(!seeking.align(3).has_value());
    CONTRACT_EXPECT_EQ(seeking.align(3).error().code, BinaryErrorCode::invalid_alignment);

    CONTRACT_EXPECT(seeking.seek(8).has_value());
    CONTRACT_EXPECT(seeking.align(8).has_value());

    return contract::test::finish();
}
