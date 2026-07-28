#pragma once

#include <contract/core/Result.hpp>
#include <contract/runtime/RuntimeHost.hpp>
#include <contract/scene/Scene.hpp>
#include <contract/scene/RenderScene.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace contract::runtime {

struct RuntimeRunnerOptions {
    std::optional<std::uint64_t> maximum_frames;
    const scene::RenderScene* render_scene{nullptr};
    std::optional<scene::EntityId> controlled_entity;
};

enum class RuntimeRunnerErrorCode {
    platform_failed,
    host_advance_failed
};

struct RuntimeRunnerError {
    RuntimeRunnerErrorCode code{RuntimeRunnerErrorCode::platform_failed};
    std::string message;
};

class IRuntimeRunner {
public:
    virtual ~IRuntimeRunner() = default;

    [[nodiscard]] virtual core::Result<void, RuntimeRunnerError> run(
        RuntimeHost& host,
        const RuntimeRunnerOptions& options) = 0;
};

}
