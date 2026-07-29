#pragma once

#include <contract/core/Result.hpp>
#include <contract/datasource/DataSource.hpp>
#include <contract/formats/PrimitiveContainer.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace contract::formats {

enum class PrimitiveRigDecodeErrorCode {
    invalid_model_record,
    unsupported_layout,
    invalid_hierarchy,
    limit_exceeded,
    source_error
};

struct PrimitiveRigDecodeError {
    PrimitiveRigDecodeErrorCode code{
        PrimitiveRigDecodeErrorCode::unsupported_layout};
    std::uint64_t offset{0};
    std::string message;
};

struct PrimitiveRigDecodeLimits {
    std::size_t max_bones{1024};
};

struct PrimitiveRigBone {
    std::string name;
    std::optional<std::size_t> parent_index;
    std::array<float, 3> reference_position{0.0F, 0.0F, 0.0F};
    std::array<float, 9> reference_basis{
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F
    };
};

struct PrimitiveRig {
    std::uint32_t model_record{0};
    std::vector<PrimitiveRigBone> bones;
};

class PrimitiveRigDecoder {
public:
    [[nodiscard]] static core::Result<
        PrimitiveRig,
        PrimitiveRigDecodeError>
    decode(
        const PrimitiveContainerIndex& container,
        const datasource::IReadOnlyDataSource& source,
        std::uint32_t model_record,
        datasource::ReadBudget& budget,
        PrimitiveRigDecodeLimits limits = {});
};

}
