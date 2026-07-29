#include <contract/formats/PrimitiveSceneDecoder.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace contract::formats {
namespace {

constexpr std::size_t kModelRecordSize = 64;
constexpr std::size_t kObjectRecordMinimumSize = 44;
constexpr std::size_t kMeshDescriptorSize = 16;
constexpr std::size_t kIndexHeaderSize = 4;
constexpr std::size_t kVertexRecordAlignment = 16;

std::optional<std::size_t> vertex_stride(
    std::size_t record_size,
    std::size_t vertex_count) {
    constexpr std::array<std::size_t, 4> supported{
        16U,
        36U,
        40U,
        52U
    };
    std::optional<std::size_t> matched;
    for (const auto stride : supported) {
        if (vertex_count >
            std::numeric_limits<std::size_t>::max() / stride) {
            continue;
        }
        const auto required = vertex_count * stride;
        if (required >
            std::numeric_limits<std::size_t>::max() -
                (kVertexRecordAlignment - 1U)) {
            continue;
        }
        const auto aligned =
            (required + kVertexRecordAlignment - 1U) &
            ~(kVertexRecordAlignment - 1U);
        if (record_size != required && record_size != aligned) {
            continue;
        }
        if (matched.has_value()) {
            return std::nullopt;
        }
        matched = stride;
    }
    return matched;
}

std::uint16_t read_u16(
    const std::vector<std::byte>& bytes,
    std::size_t offset) {
    const auto low = std::to_integer<std::uint16_t>(bytes[offset]);
    const auto high = std::to_integer<std::uint16_t>(bytes[offset + 1U]);
    return static_cast<std::uint16_t>(low | (high << 8U));
}

std::uint32_t read_u32(
    const std::vector<std::byte>& bytes,
    std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + index])
            << (index * 8U);
    }
    return value;
}

float read_f32(
    const std::vector<std::byte>& bytes,
    std::size_t offset) {
    return std::bit_cast<float>(read_u32(bytes, offset));
}

bool valid_bounds(const std::vector<std::byte>& model) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto minimum = read_f32(model, 32U + axis * 4U);
        const auto maximum = read_f32(model, 44U + axis * 4U);
        if (!std::isfinite(minimum) || !std::isfinite(maximum) ||
            minimum > maximum) {
            return false;
        }
    }
    return true;
}

core::Result<std::vector<std::byte>, PrimitiveSceneDecodeError> read_record(
    const PrimitiveContainerIndex& container,
    const datasource::IReadOnlyDataSource& source,
    std::size_t index,
    datasource::ReadBudget& budget) {
    const auto record = container.read_record(source, index, budget);
    if (!record.has_value()) {
        return core::Result<
            std::vector<std::byte>,
            PrimitiveSceneDecodeError>::failure(
            {
                PrimitiveSceneDecodeErrorCode::source_error,
                record.error().offset,
                record.error().message
            });
    }
    return core::Result<
        std::vector<std::byte>,
        PrimitiveSceneDecodeError>::success(
        std::move(record.value()));
}

core::Result<std::optional<PrimitiveMesh>, PrimitiveSceneDecodeError>
decode_object(
    const PrimitiveContainerIndex& container,
    const datasource::IReadOnlyDataSource& source,
    std::uint32_t model_record,
    std::uint32_t object_record,
    datasource::ReadBudget& budget,
    const PrimitiveSceneDecodeLimits& limits) {
    auto object = read_record(
        container,
        source,
        object_record,
        budget);
    if (!object.has_value()) {
        return core::Result<
            std::optional<PrimitiveMesh>,
            PrimitiveSceneDecodeError>::failure(
            object.error());
    }
    if (object.value().size() < kObjectRecordMinimumSize) {
        return core::Result<
            std::optional<PrimitiveMesh>,
            PrimitiveSceneDecodeError>::success(
            std::nullopt);
    }
    const auto level_of_detail =
        std::to_integer<std::uint8_t>(object.value()[14U]);
    if ((level_of_detail & 1U) == 0U) {
        return core::Result<
            std::optional<PrimitiveMesh>,
            PrimitiveSceneDecodeError>::success(
            std::nullopt);
    }

    const auto indirection_index = read_u32(object.value(), 40);
    if (indirection_index >= container.records().size()) {
        return core::Result<
            std::optional<PrimitiveMesh>,
            PrimitiveSceneDecodeError>::success(
            std::nullopt);
    }
    auto indirection = read_record(
        container,
        source,
        indirection_index,
        budget);
    if (!indirection.has_value()) {
        return core::Result<
            std::optional<PrimitiveMesh>,
            PrimitiveSceneDecodeError>::failure(
            indirection.error());
    }
    if (indirection.value().size() < 4) {
        return core::Result<
            std::optional<PrimitiveMesh>,
            PrimitiveSceneDecodeError>::success(
            std::nullopt);
    }

    const auto descriptor_index = read_u32(indirection.value(), 0);
    if (descriptor_index >= container.records().size()) {
        return core::Result<
            std::optional<PrimitiveMesh>,
            PrimitiveSceneDecodeError>::success(
            std::nullopt);
    }
    auto descriptor = read_record(
        container,
        source,
        descriptor_index,
        budget);
    if (!descriptor.has_value()) {
        return core::Result<
            std::optional<PrimitiveMesh>,
            PrimitiveSceneDecodeError>::failure(
            descriptor.error());
    }
    if (descriptor.value().size() < kMeshDescriptorSize) {
        return core::Result<
            std::optional<PrimitiveMesh>,
            PrimitiveSceneDecodeError>::success(
            std::nullopt);
    }

    const auto vertex_count = read_u32(descriptor.value(), 0);
    const auto vertex_record = read_u32(descriptor.value(), 4);
    const auto index_record = read_u32(descriptor.value(), 12);
    if (vertex_count == 0 ||
        vertex_count > limits.max_vertices_per_mesh ||
        vertex_record >= container.records().size() ||
        index_record >= container.records().size()) {
        return core::Result<
            std::optional<PrimitiveMesh>,
            PrimitiveSceneDecodeError>::success(
            std::nullopt);
    }

    auto vertices = read_record(
        container,
        source,
        vertex_record,
        budget);
    auto indices = read_record(
        container,
        source,
        index_record,
        budget);
    if (!vertices.has_value()) {
        return core::Result<
            std::optional<PrimitiveMesh>,
            PrimitiveSceneDecodeError>::failure(
            vertices.error());
    }
    if (!indices.has_value()) {
        return core::Result<
            std::optional<PrimitiveMesh>,
            PrimitiveSceneDecodeError>::failure(
            indices.error());
    }
    if (indices.value().size() < kIndexHeaderSize) {
        return core::Result<
            std::optional<PrimitiveMesh>,
            PrimitiveSceneDecodeError>::success(
            std::nullopt);
    }

    const auto detected_stride =
        vertex_stride(vertices.value().size(), vertex_count);
    if (!detected_stride.has_value()) {
        return core::Result<
            std::optional<PrimitiveMesh>,
            PrimitiveSceneDecodeError>::success(
            std::nullopt);
    }
    const auto stride = detected_stride.value();
    const auto index_count = read_u16(indices.value(), 2);
    if (index_count == 0 ||
        index_count > limits.max_indices_per_mesh ||
        static_cast<std::size_t>(index_count) >
            (indices.value().size() - kIndexHeaderSize) / 2U) {
        return core::Result<
            std::optional<PrimitiveMesh>,
            PrimitiveSceneDecodeError>::success(
            std::nullopt);
    }

    PrimitiveMesh mesh;
    mesh.model_record = model_record;
    mesh.object_record = object_record;
    mesh.vertex_stride = static_cast<std::uint32_t>(stride);
    mesh.material_id = read_u16(object.value(), 18U);
    mesh.positions.reserve(vertex_count);
    std::optional<std::size_t> texture_coordinate_offset;
    if (stride == 36U) {
        texture_coordinate_offset = 28U;
    } else if (stride == 40U) {
        texture_coordinate_offset = 20U;
    } else if (stride == 52U) {
        texture_coordinate_offset = 36U;
    }
    if (texture_coordinate_offset.has_value()) {
        mesh.texture_coordinates.reserve(vertex_count);
    }
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        const auto vertex_offset = vertex * stride;
        const PrimitivePosition position{
            read_f32(vertices.value(), vertex_offset),
            read_f32(vertices.value(), vertex_offset + 4U),
            read_f32(vertices.value(), vertex_offset + 8U)
        };
        if (!std::isfinite(position.x) ||
            !std::isfinite(position.y) ||
            !std::isfinite(position.z)) {
            return core::Result<
                std::optional<PrimitiveMesh>,
                PrimitiveSceneDecodeError>::success(
                std::nullopt);
        }
        mesh.positions.push_back(position);
        if (texture_coordinate_offset.has_value()) {
            const PrimitiveTextureCoordinate coordinate{
                read_f32(
                    vertices.value(),
                    vertex_offset +
                        texture_coordinate_offset.value()),
                read_f32(
                    vertices.value(),
                    vertex_offset +
                        texture_coordinate_offset.value() + 4U)
            };
            if (!std::isfinite(coordinate.u) ||
                !std::isfinite(coordinate.v)) {
                return core::Result<
                    std::optional<PrimitiveMesh>,
                    PrimitiveSceneDecodeError>::success(
                    std::nullopt);
            }
            mesh.texture_coordinates.push_back(coordinate);
        }
    }
    if (stride == 52U) {
        constexpr float weight_tolerance = 0.0001F;
        mesh.skinning.reserve(vertex_count);
        for (std::size_t vertex = 0;
             vertex < vertex_count;
             ++vertex) {
            const auto vertex_offset = vertex * stride;
            const auto first = read_f32(
                vertices.value(),
                vertex_offset + 12U);
            const auto second = read_f32(
                vertices.value(),
                vertex_offset + 16U);
            const auto third = read_f32(
                vertices.value(),
                vertex_offset + 20U);
            const auto fourth =
                1.0F - first - second - third;
            const std::array<float, 4> weights{
                first,
                second,
                third,
                std::clamp(fourth, 0.0F, 1.0F)
            };
            const auto valid_weight =
                std::isfinite(first) &&
                std::isfinite(second) &&
                std::isfinite(third) &&
                first >= -weight_tolerance &&
                second >= -weight_tolerance &&
                third >= -weight_tolerance &&
                fourth >= -weight_tolerance &&
                first <= 1.0F + weight_tolerance &&
                second <= 1.0F + weight_tolerance &&
                third <= 1.0F + weight_tolerance;
            if (!valid_weight) {
                mesh.skinning.clear();
                break;
            }
            mesh.skinning.push_back(
                {
                    {
                        std::to_integer<std::uint8_t>(
                            vertices.value()[
                                vertex_offset + 24U]),
                        std::to_integer<std::uint8_t>(
                            vertices.value()[
                                vertex_offset + 25U]),
                        std::to_integer<std::uint8_t>(
                            vertices.value()[
                                vertex_offset + 26U]),
                        std::to_integer<std::uint8_t>(
                            vertices.value()[
                                vertex_offset + 27U])
                    },
                    weights
                });
        }
    }

    mesh.indices.reserve(index_count);
    for (std::size_t index = 0; index < index_count; ++index) {
        const auto value = read_u16(
            indices.value(),
            kIndexHeaderSize + index * 2U);
        if (value >= vertex_count) {
            return core::Result<
                std::optional<PrimitiveMesh>,
                PrimitiveSceneDecodeError>::success(
                std::nullopt);
        }
        mesh.indices.push_back(value);
    }
    return core::Result<
        std::optional<PrimitiveMesh>,
        PrimitiveSceneDecodeError>::success(
        std::move(mesh));
}

}

core::Result<PrimitiveSceneDecodeResult, PrimitiveSceneDecodeError>
PrimitiveSceneDecoder::decode(
    const PrimitiveContainerIndex& container,
    const datasource::IReadOnlyDataSource& source,
    datasource::ReadBudget& budget,
    PrimitiveSceneDecodeLimits limits) {
    PrimitiveSceneDecodeResult result;
    std::size_t total_vertices = 0;
    std::size_t total_indices = 0;

    for (std::size_t model_index = 0;
         model_index < container.records().size();
         ++model_index) {
        if (container.records()[model_index].size != kModelRecordSize) {
            continue;
        }
        auto model = container.read_record(
            source,
            model_index,
            budget);
        if (!model.has_value()) {
            return core::Result<
                PrimitiveSceneDecodeResult,
                PrimitiveSceneDecodeError>::failure(
                {
                    PrimitiveSceneDecodeErrorCode::source_error,
                    model.error().offset,
                    model.error().message
                });
        }

        const auto parts_record = read_u32(model.value(), 16);
        const auto object_count = read_u32(model.value(), 20);
        const auto objects_record = read_u32(model.value(), 24);
        if (object_count == 0 ||
            parts_record >= container.records().size() ||
            objects_record >= container.records().size() ||
            !valid_bounds(model.value())) {
            continue;
        }
        ++result.candidate_models;
        if (object_count > limits.max_meshes) {
            ++result.rejected_models;
            continue;
        }

        auto object_list = container.read_record(
            source,
            objects_record,
            budget);
        if (!object_list.has_value()) {
            return core::Result<
                PrimitiveSceneDecodeResult,
                PrimitiveSceneDecodeError>::failure(
                {
                    PrimitiveSceneDecodeErrorCode::source_error,
                    object_list.error().offset,
                    object_list.error().message
                });
        }
        if (object_count >
            object_list.value().size() / sizeof(std::uint32_t)) {
            ++result.rejected_models;
            continue;
        }

        const auto meshes_before = result.meshes.size();
        for (std::size_t object_index = 0;
             object_index < object_count;
             ++object_index) {
            const auto object_record = read_u32(
                object_list.value(),
                object_index * sizeof(std::uint32_t));
            if (object_record >= container.records().size()) {
                ++result.rejected_objects;
                continue;
            }
            auto mesh = decode_object(
                container,
                source,
                static_cast<std::uint32_t>(model_index),
                object_record,
                budget,
                limits);
            if (!mesh.has_value()) {
                return core::Result<
                    PrimitiveSceneDecodeResult,
                    PrimitiveSceneDecodeError>::failure(
                    mesh.error());
            }
            if (!mesh.value().has_value()) {
                ++result.rejected_objects;
                continue;
            }
            if (result.meshes.size() >= limits.max_meshes ||
                mesh.value()->positions.size() >
                    limits.max_total_vertices - total_vertices ||
                mesh.value()->indices.size() >
                    limits.max_total_indices - total_indices) {
                return core::Result<
                    PrimitiveSceneDecodeResult,
                    PrimitiveSceneDecodeError>::failure(
                    {
                        PrimitiveSceneDecodeErrorCode::limit_exceeded,
                        container.records()[model_index].offset,
                        "Decoded primitive scene exceeds configured limits"
                    });
            }
            total_vertices += mesh.value()->positions.size();
            total_indices += mesh.value()->indices.size();
            result.meshes.push_back(std::move(*mesh.value()));
        }
        if (result.meshes.size() == meshes_before) {
            ++result.rejected_models;
        }
    }

    if (result.meshes.empty()) {
        return core::Result<
            PrimitiveSceneDecodeResult,
            PrimitiveSceneDecodeError>::failure(
            {
                PrimitiveSceneDecodeErrorCode::no_meshes,
                0,
                "Primitive container did not contain a supported mesh chain"
            });
    }
    return core::Result<
        PrimitiveSceneDecodeResult,
        PrimitiveSceneDecodeError>::success(
        std::move(result));
}

}
