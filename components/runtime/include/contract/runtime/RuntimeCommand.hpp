#pragma once

#include <contract/core/Result.hpp>
#include <contract/mission/Mission.hpp>
#include <contract/runtime/RuntimeWorld.hpp>
#include <contract/scene/Scene.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace contract::runtime {

inline constexpr std::size_t default_runtime_command_limit = 1024;

struct SetEntityTransformCommand {
    scene::EntityId entity;
    scene::Transform transform;
};

struct SetEntityEnabledCommand {
    scene::EntityId entity;
    bool enabled{true};
};

struct CompleteObjectiveCommand {
    mission::ObjectiveId objective;
};

struct FailObjectiveCommand {
    mission::ObjectiveId objective;
};

using RuntimeCommand = std::variant<
    SetEntityTransformCommand,
    SetEntityEnabledCommand,
    CompleteObjectiveCommand,
    FailObjectiveCommand>;

struct EntityTransformChangedEvent {
    scene::EntityId entity;
    scene::Transform transform;
};

struct EntityEnabledChangedEvent {
    scene::EntityId entity;
    bool enabled{true};
};

struct ObjectiveProgressChangedEvent {
    mission::ObjectiveId objective;
    ObjectiveProgress progress{ObjectiveProgress::pending};
};

using RuntimeEvent = std::variant<
    EntityTransformChangedEvent,
    EntityEnabledChangedEvent,
    ObjectiveProgressChangedEvent>;

struct RuntimeCommandBatchResult {
    std::vector<RuntimeEvent> events;
};

enum class RuntimeCommandErrorCode {
    command_limit_exceeded,
    command_failed
};

struct RuntimeCommandError {
    RuntimeCommandErrorCode code{
        RuntimeCommandErrorCode::command_limit_exceeded};
    std::size_t command_index{0};
    std::optional<RuntimeWorldError> world_error;
    std::string message;
};

class RuntimeCommandProcessor {
public:
    [[nodiscard]] core::Result<RuntimeCommandBatchResult, RuntimeCommandError>
    apply_atomic(
        RuntimeWorld& world,
        std::span<const RuntimeCommand> commands,
        std::size_t maximum_commands = default_runtime_command_limit) const;
};

}
