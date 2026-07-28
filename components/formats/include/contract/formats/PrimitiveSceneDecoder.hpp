#pragma once

#include <contract/core/Result.hpp>
#include <contract/datasource/DataSource.hpp>
#include <contract/formats/PrimitiveContainer.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace contract::formats {

enum class PrimitiveSceneDecodeErrorCode {
    no_meshes,
    limit_exceeded,
    source_error
};

struct PrimitiveSceneDecodeError {
    PrimitiveSceneDecodeErrorCode code{
        PrimitiveSceneDecodeErrorCode::no_meshes};
    std::uint64_t offset{0};
    std::string message;
};

struct PrimitiveSceneDecodeLimits {
    std::size_t max_meshes{65'536};
    std::size_t max_vertices_per_mesh{1'000'000};
    std::size_t max_indices_per_mesh{3'000'000};
    std::size_t max_total_vertices{8'000'000};
    std::size_t max_total_indices{24'000'000};
};

struct PrimitivePosition {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

struct PrimitiveTextureCoordinate {
    float u{0.0F};
    float v{0.0F};
};

struct PrimitiveMesh {
    std::uint32_t model_record{0};
    std::uint32_t object_record{0};
    std::uint32_t vertex_stride{0};
    std::uint16_t material_id{0};
    std::vector<PrimitivePosition> positions;
    std::vector<PrimitiveTextureCoordinate> texture_coordinates;
    std::vector<std::uint32_t> indices;
};

struct PrimitiveSceneDecodeResult {
    std::vector<PrimitiveMesh> meshes;
    std::size_t candidate_models{0};
    std::size_t rejected_models{0};
    std::size_t rejected_objects{0};
};

class PrimitiveSceneDecoder {
public:
    [[nodiscard]] static core::Result<
        PrimitiveSceneDecodeResult,
        PrimitiveSceneDecodeError>
    decode(
        const PrimitiveContainerIndex& container,
        const datasource::IReadOnlyDataSource& source,
        datasource::ReadBudget& budget,
        PrimitiveSceneDecodeLimits limits = {});
};

}
