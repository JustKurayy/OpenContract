#include "TestSupport.hpp"

#include <contract/formats/GmsSceneDecoder.hpp>

#include <zlib.h>

#include <cstddef>
#include <cstdint>
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

void write_record(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t name_offset,
    std::uint32_t primitive_record,
    std::uint32_t type_id) {
    write_u32(bytes, offset, name_offset);
    write_u32(bytes, offset + 12U, primitive_record);
    write_u32(bytes, offset + 20U, type_id);
}

std::vector<std::byte> deflate_raw(
    const std::vector<std::byte>& source) {
    z_stream stream{};
    const auto initialized = deflateInit2(
        &stream,
        Z_BEST_SPEED,
        Z_DEFLATED,
        -MAX_WBITS,
        8,
        Z_DEFAULT_STRATEGY);
    CONTRACT_EXPECT_EQ(initialized, Z_OK);
    if (initialized != Z_OK) {
        return {};
    }

    std::vector<std::byte> compressed(
        compressBound(static_cast<uLong>(source.size())));
    stream.next_in = reinterpret_cast<Bytef*>(
        const_cast<std::byte*>(source.data()));
    stream.avail_in = static_cast<uInt>(source.size());
    stream.next_out = reinterpret_cast<Bytef*>(compressed.data());
    stream.avail_out = static_cast<uInt>(compressed.size());
    const auto status = deflate(&stream, Z_FINISH);
    CONTRACT_EXPECT_EQ(status, Z_STREAM_END);
    const auto written = stream.total_out;
    deflateEnd(&stream);
    compressed.resize(written);
    return compressed;
}

std::vector<std::byte> make_gms() {
    std::vector<std::byte> body(448, std::byte{0});
    constexpr std::uint32_t table_offset = 128;
    constexpr std::uint32_t first_record = 256;
    constexpr std::uint32_t second_record = 320;
    constexpr std::uint32_t third_record = 384;
    write_u32(body, 0, table_offset);
    write_u32(body, 12, 4);
    write_u32(body, table_offset, 3);

    write_u32(
        body,
        table_offset + 4U,
        (first_record / 4U) | (1U << 24U));
    write_u32(
        body,
        table_offset + 12U,
        second_record / 4U);
    write_u32(
        body,
        table_offset + 20U,
        (third_record / 4U) | (1U << 25U));

    write_record(body, first_record, 0, 10, 0x100001);
    write_record(body, second_record, 6, 20, 0x100002);
    write_record(body, third_record, 12, 30, 0x100003);

    const auto compressed = deflate_raw(body);
    std::vector<std::byte> packed(9, std::byte{0});
    write_u32(packed, 0, static_cast<std::uint32_t>(body.size()));
    packed.insert(packed.end(), compressed.begin(), compressed.end());
    const auto checksum = adler32(
        adler32(0L, Z_NULL, 0),
        reinterpret_cast<const Bytef*>(body.data()),
        static_cast<uInt>(body.size()));
    for (int shift = 24; shift >= 0; shift -= 8) {
        packed.push_back(static_cast<std::byte>(
            (checksum >> shift) & 0xffU));
    }
    write_u32(packed, 4, static_cast<std::uint32_t>(packed.size()));
    return packed;
}

std::vector<std::byte> make_buf() {
    constexpr std::string_view names{
        "group\0child\0sibling\0",
        20};
    std::vector<std::byte> bytes;
    bytes.reserve(names.size());
    for (const auto character : names) {
        bytes.push_back(static_cast<std::byte>(character));
    }
    return bytes;
}

}

int main() {
    const auto decoded =
        contract::formats::GmsSceneDecoder::decode(
            make_gms(),
            make_buf());
    CONTRACT_EXPECT(decoded.has_value());
    if (decoded.has_value()) {
        const auto& nodes = decoded.value().nodes;
        CONTRACT_EXPECT_EQ(nodes.size(), std::size_t{4});
        CONTRACT_EXPECT_EQ(nodes[0].name, std::string{"ROOT"});
        CONTRACT_EXPECT(!nodes[0].parent_index.has_value());
        CONTRACT_EXPECT_EQ(nodes[1].name, std::string{"group"});
        CONTRACT_EXPECT(nodes[1].visibility_group_root);
        CONTRACT_EXPECT_EQ(
            nodes[1].parent_index.value(),
            std::size_t{0});
        CONTRACT_EXPECT_EQ(nodes[2].name, std::string{"child"});
        CONTRACT_EXPECT_EQ(
            nodes[2].parent_index.value(),
            std::size_t{1});
        CONTRACT_EXPECT_EQ(nodes[3].name, std::string{"sibling"});
        CONTRACT_EXPECT_EQ(
            nodes[3].parent_index.value(),
            std::size_t{0});
        CONTRACT_EXPECT_EQ(
            nodes[2].primitive_record,
            std::uint32_t{20});
        CONTRACT_EXPECT_EQ(
            decoded.value().visibility_group_count,
            std::size_t{1});
    }

    auto truncated = make_gms();
    truncated.resize(truncated.size() - 2U);
    const auto failed =
        contract::formats::GmsSceneDecoder::decode(
            truncated,
            make_buf());
    CONTRACT_EXPECT(!failed.has_value());

    auto corrupt_checksum = make_gms();
    corrupt_checksum.back() ^= std::byte{1};
    const auto checksum_failed =
        contract::formats::GmsSceneDecoder::decode(
            corrupt_checksum,
            make_buf());
    CONTRACT_EXPECT(!checksum_failed.has_value());

    return contract::test::finish();
}
