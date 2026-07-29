#include <contract/formats/PrimitiveRigDecoder.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace contract::formats {
namespace {

constexpr std::size_t kModelRecordSize = 64;
constexpr std::size_t kPartsRecordMinimumSize = 8;
constexpr std::size_t kRigHeaderMinimumSize = 12;
constexpr std::size_t kBoneRecordSize = 64;
constexpr std::size_t kBoneParentOffset = 12;
constexpr std::size_t kBoneNameOffset = 28;
constexpr std::size_t kBoneNameCapacity = 32;
constexpr std::size_t kReferenceTransformSize = 48;
constexpr std::size_t kReferenceBasisCount = 9;
constexpr std::size_t kReferencePositionOffset = 36;

core::Result<PrimitiveRig, PrimitiveRigDecodeError> failure(
    PrimitiveRigDecodeErrorCode code,
    std::uint64_t offset,
    std::string message) {
    return core::Result<
        PrimitiveRig,
        PrimitiveRigDecodeError>::failure(
        {code, offset, std::move(message)});
}

std::uint32_t read_u32(
    const std::vector<std::byte>& bytes,
    std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= std::to_integer<std::uint32_t>(
                     bytes[offset + index])
            << (index * 8U);
    }
    return value;
}

std::int32_t read_i32(
    const std::vector<std::byte>& bytes,
    std::size_t offset) {
    return static_cast<std::int32_t>(read_u32(bytes, offset));
}

float read_f32(
    const std::vector<std::byte>& bytes,
    std::size_t offset) {
    return std::bit_cast<float>(read_u32(bytes, offset));
}

core::Result<
    std::vector<std::byte>,
    PrimitiveRigDecodeError>
read_record(
    const PrimitiveContainerIndex& container,
    const datasource::IReadOnlyDataSource& source,
    std::size_t index,
    datasource::ReadBudget& budget) {
    const auto record =
        container.read_record(source, index, budget);
    if (!record.has_value()) {
        return core::Result<
            std::vector<std::byte>,
            PrimitiveRigDecodeError>::failure(
            {
                PrimitiveRigDecodeErrorCode::source_error,
                record.error().offset,
                record.error().message
            });
    }
    return core::Result<
        std::vector<std::byte>,
        PrimitiveRigDecodeError>::success(
        std::move(record.value()));
}

std::optional<std::string> read_bone_name(
    const std::vector<std::byte>& bytes,
    std::size_t offset) {
    for (std::size_t length = 0;
         length < kBoneNameCapacity;
         ++length) {
        const auto value = bytes[offset + length];
        if (value == std::byte{0}) {
            if (length == 0) {
                return std::nullopt;
            }
            std::string name;
            name.reserve(length);
            for (std::size_t index = 0; index < length; ++index) {
                name.push_back(static_cast<char>(
                    std::to_integer<unsigned char>(
                        bytes[offset + index])));
            }
            return name;
        }
    }
    return std::nullopt;
}

}

core::Result<PrimitiveRig, PrimitiveRigDecodeError>
PrimitiveRigDecoder::decode(
    const PrimitiveContainerIndex& container,
    const datasource::IReadOnlyDataSource& source,
    std::uint32_t model_record,
    datasource::ReadBudget& budget,
    PrimitiveRigDecodeLimits limits) {
    if (model_record >= container.records().size()) {
        return failure(
            PrimitiveRigDecodeErrorCode::invalid_model_record,
            model_record,
            "Primitive rig model record is out of range");
    }

    auto model = read_record(
        container,
        source,
        model_record,
        budget);
    if (!model.has_value()) {
        return core::Result<
            PrimitiveRig,
            PrimitiveRigDecodeError>::failure(model.error());
    }
    if (model.value().size() != kModelRecordSize) {
        return failure(
            PrimitiveRigDecodeErrorCode::unsupported_layout,
            container.records()[model_record].offset,
            "Primitive rig model record has an unsupported size");
    }

    const auto parts_record = read_u32(model.value(), 16);
    if (parts_record >= container.records().size()) {
        return failure(
            PrimitiveRigDecodeErrorCode::unsupported_layout,
            container.records()[model_record].offset + 16U,
            "Primitive rig parts record is out of range");
    }
    auto parts = read_record(
        container,
        source,
        parts_record,
        budget);
    if (!parts.has_value()) {
        return core::Result<
            PrimitiveRig,
            PrimitiveRigDecodeError>::failure(parts.error());
    }
    if (parts.value().size() < kPartsRecordMinimumSize) {
        return failure(
            PrimitiveRigDecodeErrorCode::unsupported_layout,
            container.records()[parts_record].offset,
            "Primitive rig parts record is truncated");
    }

    const auto rig_header_record = read_u32(parts.value(), 4);
    if (rig_header_record >= container.records().size()) {
        return failure(
            PrimitiveRigDecodeErrorCode::unsupported_layout,
            container.records()[parts_record].offset + 4U,
            "Primitive rig header record is out of range");
    }
    auto header = read_record(
        container,
        source,
        rig_header_record,
        budget);
    if (!header.has_value()) {
        return core::Result<
            PrimitiveRig,
            PrimitiveRigDecodeError>::failure(header.error());
    }
    if (header.value().size() < kRigHeaderMinimumSize) {
        return failure(
            PrimitiveRigDecodeErrorCode::unsupported_layout,
            container.records()[rig_header_record].offset,
            "Primitive rig header is truncated");
    }

    const auto bone_count = read_u32(header.value(), 0);
    const auto bone_record = read_u32(header.value(), 4);
    const auto reference_record = read_u32(header.value(), 8);
    if (bone_count == 0U) {
        return failure(
            PrimitiveRigDecodeErrorCode::unsupported_layout,
            container.records()[rig_header_record].offset,
            "Primitive rig has no bones");
    }
    if (bone_count > limits.max_bones) {
        return failure(
            PrimitiveRigDecodeErrorCode::limit_exceeded,
            container.records()[rig_header_record].offset,
            "Primitive rig bone count exceeds configured limits");
    }
    if (bone_record >= container.records().size()) {
        return failure(
            PrimitiveRigDecodeErrorCode::unsupported_layout,
            container.records()[rig_header_record].offset + 4U,
            "Primitive rig bone record is out of range");
    }
    if (reference_record >= container.records().size()) {
        return failure(
            PrimitiveRigDecodeErrorCode::unsupported_layout,
            container.records()[rig_header_record].offset + 8U,
            "Primitive rig reference transform record is out of range");
    }
    if (bone_count >
        std::numeric_limits<std::size_t>::max() /
            kBoneRecordSize) {
        return failure(
            PrimitiveRigDecodeErrorCode::limit_exceeded,
            container.records()[rig_header_record].offset,
            "Primitive rig bone storage would overflow");
    }
    const auto expected_bone_bytes =
        static_cast<std::size_t>(bone_count) *
        kBoneRecordSize;
    if (container.records()[bone_record].size !=
        expected_bone_bytes) {
        return failure(
            PrimitiveRigDecodeErrorCode::unsupported_layout,
            container.records()[bone_record].offset,
            "Primitive rig bone record size is inconsistent");
    }
    if (bone_count >
        std::numeric_limits<std::size_t>::max() /
            kReferenceTransformSize) {
        return failure(
            PrimitiveRigDecodeErrorCode::limit_exceeded,
            container.records()[rig_header_record].offset,
            "Primitive rig reference transforms would overflow");
    }
    const auto expected_reference_bytes =
        static_cast<std::size_t>(bone_count) *
        kReferenceTransformSize;
    if (container.records()[reference_record].size !=
        expected_reference_bytes) {
        return failure(
            PrimitiveRigDecodeErrorCode::unsupported_layout,
            container.records()[reference_record].offset,
            "Primitive rig reference transform size is inconsistent");
    }

    auto bone_bytes = read_record(
        container,
        source,
        bone_record,
        budget);
    if (!bone_bytes.has_value()) {
        return core::Result<
            PrimitiveRig,
            PrimitiveRigDecodeError>::failure(
            bone_bytes.error());
    }
    auto reference_bytes = read_record(
        container,
        source,
        reference_record,
        budget);
    if (!reference_bytes.has_value()) {
        return core::Result<
            PrimitiveRig,
            PrimitiveRigDecodeError>::failure(
            reference_bytes.error());
    }

    PrimitiveRig rig;
    rig.model_record = model_record;
    rig.bones.reserve(bone_count);
    for (std::size_t index = 0;
         index < bone_count;
         ++index) {
        const auto offset = index * kBoneRecordSize;
        const auto parent = read_i32(
            bone_bytes.value(),
            offset + kBoneParentOffset);
        if ((index == 0U && parent != -1) ||
            (index != 0U &&
             (parent < 0 ||
              static_cast<std::size_t>(parent) >= index))) {
            return failure(
                PrimitiveRigDecodeErrorCode::invalid_hierarchy,
                container.records()[bone_record].offset +
                    offset + kBoneParentOffset,
                "Primitive rig parent indices are not topological");
        }
        auto name = read_bone_name(
            bone_bytes.value(),
            offset + kBoneNameOffset);
        if (!name.has_value()) {
            return failure(
                PrimitiveRigDecodeErrorCode::unsupported_layout,
                container.records()[bone_record].offset +
                    offset + kBoneNameOffset,
                "Primitive rig bone name is empty or unterminated");
        }
        const auto reference_offset =
            index * kReferenceTransformSize;
        std::array<float, kReferenceBasisCount> reference_basis{};
        for (std::size_t component = 0;
             component < reference_basis.size();
             ++component) {
            reference_basis[component] = read_f32(
                reference_bytes.value(),
                reference_offset + component * sizeof(float));
            if (!std::isfinite(reference_basis[component])) {
                return failure(
                    PrimitiveRigDecodeErrorCode::unsupported_layout,
                    container.records()[reference_record].offset +
                        reference_offset +
                        component * sizeof(float),
                    "Primitive rig reference basis is non-finite");
            }
        }
        const auto reference_position_offset =
            reference_offset + kReferencePositionOffset;
        const std::array<float, 3> reference_position{
            read_f32(
                reference_bytes.value(),
                reference_position_offset),
            read_f32(
                reference_bytes.value(),
                reference_position_offset + 4U),
            read_f32(
                reference_bytes.value(),
                reference_position_offset + 8U)
        };
        if (!std::isfinite(reference_position[0]) ||
            !std::isfinite(reference_position[1]) ||
            !std::isfinite(reference_position[2])) {
            return failure(
                PrimitiveRigDecodeErrorCode::unsupported_layout,
                container.records()[reference_record].offset +
                    reference_position_offset,
                "Primitive rig reference position is non-finite");
        }
        rig.bones.push_back(
            {
                std::move(name.value()),
                parent < 0
                    ? std::nullopt
                    : std::optional<std::size_t>{
                          static_cast<std::size_t>(parent)},
                reference_position,
                reference_basis
            });
    }

    return core::Result<
        PrimitiveRig,
        PrimitiveRigDecodeError>::success(std::move(rig));
}

}
