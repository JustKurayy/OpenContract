#include <contract/formats/GmsSceneDecoder.hpp>

#include <contract/binaryio/BinaryReader.hpp>

#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace contract::formats {
namespace {

constexpr std::size_t kPackedHeaderSize = 9;
constexpr std::size_t kSceneHeaderSize = 16;
constexpr std::size_t kTableEntrySize = 8;
constexpr std::size_t kRecordSize = 64;
constexpr std::uint32_t kRecordOffsetMask = 0x00ff'ffffU;
constexpr std::uint32_t kVisibilityGroupFlag = 1U << 24U;

core::Result<GmsSceneDecodeResult, GmsSceneError> failure(
    GmsSceneErrorCode code,
    std::uint64_t offset,
    std::string message) {
    return core::Result<
        GmsSceneDecodeResult,
        GmsSceneError>::failure(
        {code, offset, std::move(message)});
}

core::Result<std::vector<std::byte>, GmsSceneError> unpack(
    std::span<const std::byte> packed,
    const GmsSceneDecodeLimits& limits) {
    if (packed.size() < kPackedHeaderSize) {
        return core::Result<
            std::vector<std::byte>,
            GmsSceneError>::failure(
            {
                GmsSceneErrorCode::truncated,
                0,
                "GMS packed header is truncated"
            });
    }
    binaryio::BinaryReader reader(packed);
    const auto unpacked_size = reader.read_u32();
    const auto packed_size = reader.read_u32();
    const auto flags = reader.read_u8();
    if (!unpacked_size.has_value() ||
        !packed_size.has_value() ||
        !flags.has_value()) {
        return core::Result<
            std::vector<std::byte>,
            GmsSceneError>::failure(
            {
                GmsSceneErrorCode::truncated,
                0,
                "GMS packed header is truncated"
            });
    }
    if (flags.value() != 0U) {
        return core::Result<
            std::vector<std::byte>,
            GmsSceneError>::failure(
            {
                GmsSceneErrorCode::invalid_header,
                8,
                "GMS compression flags are unsupported"
            });
    }
    if (packed_size.value() != packed.size()) {
        return core::Result<
            std::vector<std::byte>,
            GmsSceneError>::failure(
            {
                GmsSceneErrorCode::invalid_header,
                4,
                "GMS packed size does not match the input"
            });
    }
    if (unpacked_size.value() < kSceneHeaderSize ||
        unpacked_size.value() > limits.max_unpacked_size) {
        return core::Result<
            std::vector<std::byte>,
            GmsSceneError>::failure(
            {
                GmsSceneErrorCode::limit_exceeded,
                0,
                "GMS unpacked size is outside configured limits"
            });
    }

    std::vector<std::byte> output(
        static_cast<std::size_t>(unpacked_size.value()) + 1U);
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(
        const_cast<std::byte*>(packed.data() + kPackedHeaderSize));
    stream.avail_in = static_cast<uInt>(
        packed.size() - kPackedHeaderSize);
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return core::Result<
            std::vector<std::byte>,
            GmsSceneError>::failure(
            {
                GmsSceneErrorCode::decompression_failed,
                kPackedHeaderSize,
                "Could not initialize GMS decompression"
            });
    }
    const auto status = inflate(&stream, Z_FINISH);
    const auto total_out = stream.total_out;
    const auto remaining_input = stream.avail_in;
    inflateEnd(&stream);
    if (status != Z_STREAM_END ||
        total_out != unpacked_size.value() ||
        (remaining_input != 0U && remaining_input != 4U)) {
        return core::Result<
            std::vector<std::byte>,
            GmsSceneError>::failure(
            {
                GmsSceneErrorCode::decompression_failed,
                kPackedHeaderSize,
                "GMS compressed payload is truncated or inconsistent "
                "(status " + std::to_string(status) +
                ", output " + std::to_string(total_out) +
                ", remaining " + std::to_string(remaining_input) + ")"
            });
    }
    output.resize(unpacked_size.value());
    if (remaining_input == 4U) {
        const auto trailer_offset = packed.size() - 4U;
        std::uint32_t declared_checksum = 0;
        for (std::size_t index = 0; index < 4; ++index) {
            declared_checksum =
                (declared_checksum << 8U) |
                std::to_integer<std::uint8_t>(
                    packed[trailer_offset + index]);
        }
        const auto actual_checksum = static_cast<std::uint32_t>(
            adler32(
                adler32(0L, Z_NULL, 0),
                reinterpret_cast<const Bytef*>(output.data()),
                static_cast<uInt>(output.size())));
        if (declared_checksum != actual_checksum) {
            return core::Result<
                std::vector<std::byte>,
                GmsSceneError>::failure(
                {
                    GmsSceneErrorCode::decompression_failed,
                    trailer_offset,
                    "GMS checksum does not match its unpacked payload"
                });
        }
    }
    return core::Result<
        std::vector<std::byte>,
        GmsSceneError>::success(std::move(output));
}

core::Result<std::string, GmsSceneError> read_name(
    std::span<const std::byte> buffer,
    std::uint32_t offset,
    std::size_t max_length) {
    if (offset >= buffer.size()) {
        return core::Result<std::string, GmsSceneError>::failure(
            {
                GmsSceneErrorCode::invalid_offset,
                offset,
                "GMS name offset exceeds the name buffer"
            });
    }
    const auto remaining = buffer.subspan(offset);
    const auto limit = std::min(remaining.size(), max_length + 1U);
    for (std::size_t length = 0; length < limit; ++length) {
        if (remaining[length] == std::byte{0}) {
            std::string name;
            name.reserve(length);
            for (std::size_t index = 0; index < length; ++index) {
                name.push_back(static_cast<char>(
                    std::to_integer<unsigned char>(
                        remaining[index])));
            }
            return core::Result<
                std::string,
                GmsSceneError>::success(std::move(name));
        }
    }
    return core::Result<std::string, GmsSceneError>::failure(
        {
            GmsSceneErrorCode::limit_exceeded,
            offset,
            "GMS name is unterminated or exceeds configured limits"
        });
}

}

core::Result<GmsSceneDecodeResult, GmsSceneError>
GmsSceneDecoder::decode(
    std::span<const std::byte> packed_gms,
    std::span<const std::byte> name_buffer,
    GmsSceneDecodeLimits limits) {
    if (packed_gms.size() > limits.max_packed_size) {
        return failure(
            GmsSceneErrorCode::limit_exceeded,
            0,
            "GMS packed input exceeds configured limits");
    }
    auto body = unpack(packed_gms, limits);
    if (!body.has_value()) {
        return core::Result<
            GmsSceneDecodeResult,
            GmsSceneError>::failure(body.error());
    }

    binaryio::BinaryReader header(body.value());
    const auto table_offset = header.read_u32();
    const auto signature_a = header.read_u32();
    const auto signature_b = header.read_u32();
    const auto signature_c = header.read_u32();
    if (!table_offset.has_value() ||
        !signature_a.has_value() ||
        !signature_b.has_value() ||
        !signature_c.has_value()) {
        return failure(
            GmsSceneErrorCode::truncated,
            0,
            "GMS scene header is truncated");
    }
    if (signature_a.value() != 0U ||
        signature_b.value() != 0U ||
        signature_c.value() != 4U) {
        return failure(
            GmsSceneErrorCode::invalid_header,
            4,
            "GMS scene signature is invalid");
    }
    if (table_offset.value() >
        body.value().size() - sizeof(std::uint32_t)) {
        return failure(
            GmsSceneErrorCode::invalid_offset,
            0,
            "GMS entity table offset exceeds the input");
    }

    binaryio::BinaryReader table(body.value());
    if (!table.seek(table_offset.value()).has_value()) {
        return failure(
            GmsSceneErrorCode::invalid_offset,
            0,
            "GMS entity table offset exceeds the input");
    }
    const auto count = table.read_u32();
    if (!count.has_value()) {
        return failure(
            GmsSceneErrorCode::truncated,
            table_offset.value(),
            "GMS entity count is truncated");
    }
    if (count.value() > limits.max_nodes - 1U) {
        return failure(
            GmsSceneErrorCode::limit_exceeded,
            table_offset.value(),
            "GMS entity count exceeds configured limits");
    }
    if (count.value() >
        (body.value().size() - table.offset()) / kTableEntrySize) {
        return failure(
            GmsSceneErrorCode::truncated,
            table.offset(),
            "GMS entity table is truncated");
    }

    GmsSceneDecodeResult result;
    result.nodes.reserve(static_cast<std::size_t>(count.value()) + 1U);
    result.nodes.push_back(
        {
            "ROOT",
            0,
            0x100021U,
            std::nullopt,
            0,
            true,
            0
        });
    std::vector<std::size_t> parent_path{0};
    parent_path.reserve(128);

    for (std::uint32_t index = 0; index < count.value(); ++index) {
        const auto description_offset = table.offset();
        const auto encoded_offset = table.read_u32();
        const auto control = table.read_u32();
        if (!encoded_offset.has_value() || !control.has_value()) {
            return failure(
                GmsSceneErrorCode::truncated,
                description_offset,
                "GMS entity description is truncated");
        }
        static_cast<void>(control);
        const auto record_word =
            encoded_offset.value() & kRecordOffsetMask;
        if (record_word >
            std::numeric_limits<std::size_t>::max() / 4U) {
            return failure(
                GmsSceneErrorCode::invalid_offset,
                description_offset,
                "GMS entity record offset would overflow");
        }
        const auto record_offset =
            static_cast<std::size_t>(record_word) * 4U;
        if (record_offset > body.value().size() ||
            kRecordSize > body.value().size() - record_offset) {
            return failure(
                GmsSceneErrorCode::invalid_offset,
                description_offset,
                "GMS entity record exceeds the input");
        }

        binaryio::BinaryReader record(body.value());
        static_cast<void>(record.seek(record_offset));
        const auto name_offset = record.read_u32();
        static_cast<void>(record.seek(record_offset + 12U));
        const auto primitive_record = record.read_u32();
        static_cast<void>(record.seek(record_offset + 20U));
        const auto type_id = record.read_u32();
        if (!name_offset.has_value() ||
            !primitive_record.has_value() ||
            !type_id.has_value()) {
            return failure(
                GmsSceneErrorCode::truncated,
                record_offset,
                "GMS entity record is truncated");
        }
        auto name = read_name(
            name_buffer,
            name_offset.value(),
            limits.max_name_length);
        if (!name.has_value()) {
            return core::Result<
                GmsSceneDecodeResult,
                GmsSceneError>::failure(name.error());
        }

        const auto relative_depth = static_cast<std::uint8_t>(
            encoded_offset.value() >> 25U);
        if (relative_depth >= parent_path.size()) {
            return failure(
                GmsSceneErrorCode::invalid_hierarchy,
                description_offset,
                "GMS relative depth exits above the scene root");
        }
        parent_path.resize(
            parent_path.size() -
            static_cast<std::size_t>(relative_depth));
        const auto parent = parent_path.back();
        const auto visibility_group =
            (encoded_offset.value() & kVisibilityGroupFlag) != 0U;
        const auto node_index = result.nodes.size();
        result.nodes.push_back(
            {
                std::move(name.value()),
                primitive_record.value(),
                type_id.value(),
                parent,
                relative_depth,
                visibility_group,
                record_offset
            });
        if (visibility_group) {
            parent_path.push_back(node_index);
            ++result.visibility_group_count;
        }
    }

    return core::Result<
        GmsSceneDecodeResult,
        GmsSceneError>::success(std::move(result));
}

}
