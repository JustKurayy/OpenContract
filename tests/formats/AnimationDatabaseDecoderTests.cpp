#include "TestSupport.hpp"

#include <contract/datasource/DataSource.hpp>
#include <contract/formats/AnimationDatabaseDecoder.hpp>
#include <contract/formats/AnimationTrackDirectory.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace {

void write_u16(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint16_t value) {
    for (std::size_t index = 0; index < 2; ++index) {
        bytes[offset + index] = static_cast<std::byte>(
            (value >> (index * 8U)) & 0xffU);
    }
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

void write_text(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::string_view text) {
    for (std::size_t index = 0; index < text.size(); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(text[index]);
    }
}

std::vector<std::byte> make_database_chunk() {
    constexpr std::string_view name =
        "anmcol:animationdatabase#synthetic";
    const auto aligned_name_size =
        (name.size() + 1U + 3U) & ~std::size_t{3U};
    std::vector<std::byte> payload(
        aligned_name_size + 4U + 2U * 8U,
        std::byte{0});
    write_text(payload, 0, name);
    write_u32(payload, aligned_name_size, 2);
    write_u32(payload, aligned_name_size + 4U, 0);
    write_u32(payload, aligned_name_size + 8U, 1);
    write_u32(payload, aligned_name_size + 12U, 0);
    write_u32(payload, aligned_name_size + 16U, 0);
    return payload;
}

std::vector<std::byte> make_path_chunk() {
    constexpr std::string_view idle = "/idle.synthetic";
    constexpr std::string_view move = "/move.synthetic";
    constexpr std::size_t table_size = 4U + 2U * 8U;
    std::vector<std::byte> payload(
        table_size + idle.size() + 1U + move.size() + 1U,
        std::byte{0});
    write_u32(payload, 0, 2);
    write_u32(payload, 4, 0);
    write_u32(payload, 8, 0);
    write_u32(
        payload,
        12,
        static_cast<std::uint32_t>(idle.size() + 1U));
    write_u32(payload, 16, 1);
    write_text(payload, table_size, idle);
    write_text(payload, table_size + idle.size() + 1U, move);
    return payload;
}

std::vector<std::byte> make_clip_chunk() {
    constexpr std::size_t header_size = 32;
    constexpr std::size_t descriptor_size = 64;
    constexpr std::size_t clip_count = 2;
    constexpr std::size_t encoded_size = 8;
    std::vector<std::byte> payload(
        header_size + clip_count * descriptor_size + encoded_size,
        std::byte{0});
    write_u32(payload, 0, clip_count);
    write_u32(payload, 4, encoded_size);
    write_u32(payload, 24, clip_count);

    const auto first = header_size;
    write_u32(payload, first + 24U, (25U << 16U) | 5U);
    write_u32(payload, first + 28U, 3);
    write_u32(payload, first + 32U, 4);
    write_u32(payload, first + 36U, 0);
    write_u32(
        payload,
        first + 40U,
        std::numeric_limits<std::uint32_t>::max());
    write_u32(
        payload,
        first + 44U,
        std::numeric_limits<std::uint32_t>::max());
    write_u32(
        payload,
        first + 52U,
        std::numeric_limits<std::uint32_t>::max());

    const auto second = first + descriptor_size;
    write_u32(payload, second + 24U, (20U << 16U) | 10U);
    write_u32(payload, second + 28U, (2U << 16U) | 5U);
    write_u32(payload, second + 32U, 4);
    write_u32(payload, second + 36U, 4);
    write_u32(
        payload,
        second + 40U,
        std::numeric_limits<std::uint32_t>::max());
    write_u32(
        payload,
        second + 44U,
        std::numeric_limits<std::uint32_t>::max());
    write_u32(
        payload,
        second + 52U,
        std::numeric_limits<std::uint32_t>::max());

    for (std::size_t index = 0; index < encoded_size; ++index) {
        payload[header_size + clip_count * descriptor_size + index] =
            static_cast<std::byte>(index + 1U);
    }
    return payload;
}

void append_chunk(
    std::vector<std::byte>& bytes,
    std::uint32_t kind,
    std::span<const std::byte> payload) {
    const auto start = bytes.size();
    bytes.resize(start + 8U + payload.size(), std::byte{0});
    write_u32(bytes, start, kind);
    write_u32(
        bytes,
        start + 4U,
        static_cast<std::uint32_t>(8U + payload.size()));
    for (std::size_t index = 0; index < payload.size(); ++index) {
        bytes[start + 8U + index] = payload[index];
    }
}

std::vector<std::byte> make_database() {
    std::vector<std::byte> bytes(16, std::byte{0});
    bytes[0] = std::byte{'M'};
    bytes[1] = std::byte{'N'};
    bytes[2] = std::byte{'A'};
    const auto database = make_database_chunk();
    const auto paths = make_path_chunk();
    const auto clips = make_clip_chunk();
    append_chunk(bytes, 6, database);
    append_chunk(bytes, 7, paths);
    append_chunk(bytes, 4, clips);
    const auto size = static_cast<std::uint32_t>(bytes.size());
    write_u32(bytes, 4, size | 0x80000000U);
    write_u32(bytes, 8, size);
    write_u32(bytes, 12, 3);
    return bytes;
}

}

int main() {
    using contract::formats::AnimationDatabaseDecodeErrorCode;
    using contract::formats::AnimationDatabaseIndex;

    const auto bytes = make_database();
    contract::datasource::MemoryDataSource source(bytes);
    contract::datasource::ReadBudget budget(64U * 1024U, 4096);
    const auto decoded = AnimationDatabaseIndex::read(source, budget);
    CONTRACT_EXPECT(decoded.has_value());
    if (decoded.has_value()) {
        CONTRACT_EXPECT_EQ(decoded.value().paths().size(), std::size_t{2});
        CONTRACT_EXPECT_EQ(
            decoded.value().databases().size(),
            std::size_t{1});
        CONTRACT_EXPECT_EQ(decoded.value().clips().size(), std::size_t{2});

        const auto idle = decoded.value().resolve(
            "anmcol:animationdatabase#synthetic",
            "/idle.synthetic");
        CONTRACT_EXPECT(idle.has_value());
        if (idle.has_value()) {
            CONTRACT_EXPECT_EQ(idle.value().index, std::uint32_t{1});
            CONTRACT_EXPECT_EQ(
                idle.value().sample_count,
                std::uint16_t{10});
            CONTRACT_EXPECT_EQ(
                idle.value().samples_per_second,
                std::uint16_t{20});
            CONTRACT_EXPECT_EQ(
                idle.value().track_count,
                std::uint16_t{5});
            CONTRACT_EXPECT_EQ(idle.value().track_flags, std::uint16_t{2});
            CONTRACT_EXPECT_EQ(idle.value().duration_seconds(), 0.5F);

            contract::datasource::ReadBudget read_budget(64, 16);
            const auto encoded = decoded.value().read_encoded_clip(
                source,
                idle.value(),
                read_budget);
            CONTRACT_EXPECT(encoded.has_value());
            if (encoded.has_value()) {
                CONTRACT_EXPECT_EQ(encoded.value().size(), std::size_t{4});
                CONTRACT_EXPECT_EQ(encoded.value()[0], std::byte{5});
                CONTRACT_EXPECT_EQ(encoded.value()[3], std::byte{8});
            }

            auto forged = idle.value();
            ++forged.encoded_offset;
            contract::datasource::ReadBudget forged_budget(64, 16);
            const auto forged_read = decoded.value().read_encoded_clip(
                source,
                forged,
                forged_budget);
            CONTRACT_EXPECT(!forged_read.has_value());
            if (!forged_read.has_value()) {
                CONTRACT_EXPECT_EQ(
                    forged_read.error().code,
                    AnimationDatabaseDecodeErrorCode::invalid_clip);
            }
        }

        const auto move = decoded.value().resolve(
            "anmcol:animationdatabase#synthetic",
            "/move.synthetic");
        CONTRACT_EXPECT(move.has_value());
        if (move.has_value()) {
            CONTRACT_EXPECT_EQ(move.value().index, std::uint32_t{0});
            CONTRACT_EXPECT_EQ(move.value().duration_seconds(), 0.2F);
        }

        const auto missing_database = decoded.value().resolve(
            "anmcol:animationdatabase#missing",
            "/idle.synthetic");
        CONTRACT_EXPECT(!missing_database.has_value());
        if (!missing_database.has_value()) {
            CONTRACT_EXPECT_EQ(
                missing_database.error().code,
                AnimationDatabaseDecodeErrorCode::database_not_found);
        }

        const auto missing_path = decoded.value().resolve(
            "anmcol:animationdatabase#synthetic",
            "/missing.synthetic");
        CONTRACT_EXPECT(!missing_path.has_value());
        if (!missing_path.has_value()) {
            CONTRACT_EXPECT_EQ(
                missing_path.error().code,
                AnimationDatabaseDecodeErrorCode::path_not_found);
        }
    }

    auto mismatched_size = make_database();
    write_u32(mismatched_size, 8, 17);
    contract::datasource::MemoryDataSource mismatched_source(
        mismatched_size);
    contract::datasource::ReadBudget mismatched_budget(
        64U * 1024U,
        4096);
    const auto mismatched = AnimationDatabaseIndex::read(
        mismatched_source,
        mismatched_budget);
    CONTRACT_EXPECT(!mismatched.has_value());
    if (!mismatched.has_value()) {
        CONTRACT_EXPECT_EQ(
            mismatched.error().code,
            AnimationDatabaseDecodeErrorCode::invalid_header);
    }

    auto bad_path = make_database();
    const auto database_chunk_size =
        8U + make_database_chunk().size();
    const auto path_payload = 16U + database_chunk_size + 8U;
    write_u32(bad_path, path_payload + 4U, 0xfffffff0U);
    contract::datasource::MemoryDataSource bad_path_source(bad_path);
    contract::datasource::ReadBudget bad_path_budget(
        64U * 1024U,
        4096);
    const auto invalid_path = AnimationDatabaseIndex::read(
        bad_path_source,
        bad_path_budget);
    CONTRACT_EXPECT(!invalid_path.has_value());
    if (!invalid_path.has_value()) {
        CONTRACT_EXPECT_EQ(
            invalid_path.error().code,
            AnimationDatabaseDecodeErrorCode::invalid_path);
    }

    auto duplicate_path = make_database();
    write_u32(duplicate_path, path_payload + 16U, 0);
    contract::datasource::MemoryDataSource duplicate_path_source(
        duplicate_path);
    contract::datasource::ReadBudget duplicate_path_budget(
        64U * 1024U,
        4096);
    const auto duplicate = AnimationDatabaseIndex::read(
        duplicate_path_source,
        duplicate_path_budget);
    CONTRACT_EXPECT(!duplicate.has_value());
    if (!duplicate.has_value()) {
        CONTRACT_EXPECT_EQ(
            duplicate.error().code,
            AnimationDatabaseDecodeErrorCode::duplicate_path);
    }

    auto bad_clip = make_database();
    const auto path_chunk_size = 8U + make_path_chunk().size();
    const auto clip_payload =
        16U + database_chunk_size + path_chunk_size + 8U;
    write_u32(bad_clip, clip_payload + 32U + 36U, 9);
    contract::datasource::MemoryDataSource bad_clip_source(bad_clip);
    contract::datasource::ReadBudget bad_clip_budget(
        64U * 1024U,
        4096);
    const auto invalid_clip = AnimationDatabaseIndex::read(
        bad_clip_source,
        bad_clip_budget);
    CONTRACT_EXPECT(!invalid_clip.has_value());
    if (!invalid_clip.has_value()) {
        CONTRACT_EXPECT_EQ(
            invalid_clip.error().code,
            AnimationDatabaseDecodeErrorCode::invalid_clip);
    }

    contract::datasource::MemoryDataSource limited_source(bytes);
    contract::datasource::ReadBudget limited_budget(
        64U * 1024U,
        4096);
    const auto limited = AnimationDatabaseIndex::read(
        limited_source,
        limited_budget,
        {.max_clips = 1});
    CONTRACT_EXPECT(!limited.has_value());
    if (!limited.has_value()) {
        CONTRACT_EXPECT_EQ(
            limited.error().code,
            AnimationDatabaseDecodeErrorCode::limit_exceeded);
    }

    auto truncated_chunk = make_database();
    truncated_chunk.pop_back();
    const auto truncated_size =
        static_cast<std::uint32_t>(truncated_chunk.size());
    write_u32(
        truncated_chunk,
        4,
        truncated_size | 0x80000000U);
    write_u32(truncated_chunk, 8, truncated_size);
    contract::datasource::MemoryDataSource truncated_source(
        truncated_chunk);
    contract::datasource::ReadBudget truncated_budget(
        64U * 1024U,
        4096);
    const auto truncated = AnimationDatabaseIndex::read(
        truncated_source,
        truncated_budget);
    CONTRACT_EXPECT(!truncated.has_value());
    if (!truncated.has_value()) {
        CONTRACT_EXPECT_EQ(
            truncated.error().code,
            AnimationDatabaseDecodeErrorCode::invalid_chunk);
    }

    contract::formats::AnimationClipDescriptor routed_descriptor;
    routed_descriptor.encoded_size = 64;
    routed_descriptor.channel_offsets = {
        std::uint32_t{0},
        std::nullopt,
        std::uint32_t{32},
        std::nullopt
    };
    std::vector<std::byte> routed_bytes(64, std::byte{0});
    routed_bytes[20] = std::byte{2};
    write_u16(routed_bytes, 21, 3);
    write_u16(routed_bytes, 23, 7);
    routed_bytes[25] = std::byte{0xe0};
    routed_bytes[26] = std::byte{0xe3};
    routed_bytes[52] = std::byte{1};
    write_u16(routed_bytes, 53, 56);
    routed_bytes[55] = std::byte{0xe6};

    const auto routed =
        contract::formats::decode_animation_track_directories(
            routed_bytes,
            routed_descriptor);
    CONTRACT_EXPECT(routed.has_value());
    if (routed.has_value()) {
        CONTRACT_EXPECT_EQ(routed.value().size(), std::size_t{2});
        CONTRACT_EXPECT_EQ(
            routed.value()[0].channel_slot,
            std::uint8_t{0});
        CONTRACT_EXPECT_EQ(
            routed.value()[0].encoded_size,
            std::uint32_t{32});
        CONTRACT_EXPECT_EQ(
            routed.value()[0].payload_offset,
            std::uint32_t{27});
        CONTRACT_EXPECT_EQ(
            routed.value()[0].tracks.size(),
            std::size_t{2});
        CONTRACT_EXPECT_EQ(
            routed.value()[0].tracks[0].track_id,
            std::uint16_t{3});
        CONTRACT_EXPECT_EQ(
            routed.value()[0].tracks[1].encoding,
            std::uint8_t{0xe3});
        CONTRACT_EXPECT_EQ(
            routed.value()[1].channel_slot,
            std::uint8_t{2});
        CONTRACT_EXPECT_EQ(
            routed.value()[1].tracks[0].track_id,
            std::uint16_t{56});
    }

    auto duplicate_track_bytes = routed_bytes;
    write_u16(duplicate_track_bytes, 23, 3);
    const auto duplicate_track =
        contract::formats::decode_animation_track_directories(
            duplicate_track_bytes,
            routed_descriptor);
    CONTRACT_EXPECT(!duplicate_track.has_value());
    if (!duplicate_track.has_value()) {
        CONTRACT_EXPECT_EQ(
            duplicate_track.error().code,
            AnimationDatabaseDecodeErrorCode::invalid_clip);
    }

    auto truncated_directory_bytes = routed_bytes;
    truncated_directory_bytes[52] = std::byte{5};
    const auto truncated_directory =
        contract::formats::decode_animation_track_directories(
            truncated_directory_bytes,
            routed_descriptor);
    CONTRACT_EXPECT(!truncated_directory.has_value());
    if (!truncated_directory.has_value()) {
        CONTRACT_EXPECT_EQ(
            truncated_directory.error().code,
            AnimationDatabaseDecodeErrorCode::invalid_clip);
    }

    const auto route_limited =
        contract::formats::decode_animation_track_directories(
            routed_bytes,
            routed_descriptor,
            2);
    CONTRACT_EXPECT(!route_limited.has_value());
    if (!route_limited.has_value()) {
        CONTRACT_EXPECT_EQ(
            route_limited.error().code,
            AnimationDatabaseDecodeErrorCode::limit_exceeded);
    }

    auto unsupported_encoding_bytes = routed_bytes;
    unsupported_encoding_bytes[26] = std::byte{0xff};
    const auto unsupported_encoding =
        contract::formats::decode_animation_track_directories(
            unsupported_encoding_bytes,
            routed_descriptor);
    CONTRACT_EXPECT(!unsupported_encoding.has_value());
    if (!unsupported_encoding.has_value()) {
        CONTRACT_EXPECT_EQ(
            unsupported_encoding.error().code,
            AnimationDatabaseDecodeErrorCode::
                unsupported_track_encoding);
    }

    return contract::test::finish();
}
