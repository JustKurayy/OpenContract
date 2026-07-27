#include <contract/runtime/RuntimeCommand.hpp>

#include <type_traits>
#include <utility>

namespace contract::runtime {
namespace {

core::Result<RuntimeEvent, RuntimeWorldError> apply_command(
    RuntimeWorld& world,
    const RuntimeCommand& command) {
    return std::visit(
        [&world](const auto& value) -> core::Result<RuntimeEvent, RuntimeWorldError> {
            using Command = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Command, SetEntityTransformCommand>) {
                const auto result =
                    world.set_entity_transform(value.entity, value.transform);
                if (!result.has_value()) {
                    return core::Result<RuntimeEvent, RuntimeWorldError>::failure(
                        result.error());
                }
                return core::Result<RuntimeEvent, RuntimeWorldError>::success(
                    EntityTransformChangedEvent{
                        value.entity,
                        value.transform});
            } else if constexpr (
                std::is_same_v<Command, SetEntityEnabledCommand>) {
                const auto result =
                    world.set_entity_enabled(value.entity, value.enabled);
                if (!result.has_value()) {
                    return core::Result<RuntimeEvent, RuntimeWorldError>::failure(
                        result.error());
                }
                return core::Result<RuntimeEvent, RuntimeWorldError>::success(
                    EntityEnabledChangedEvent{
                        value.entity,
                        value.enabled});
            } else if constexpr (
                std::is_same_v<Command, CompleteObjectiveCommand>) {
                const auto result = world.complete_objective(value.objective);
                if (!result.has_value()) {
                    return core::Result<RuntimeEvent, RuntimeWorldError>::failure(
                        result.error());
                }
                return core::Result<RuntimeEvent, RuntimeWorldError>::success(
                    ObjectiveProgressChangedEvent{
                        value.objective,
                        ObjectiveProgress::completed});
            } else {
                const auto result = world.fail_objective(value.objective);
                if (!result.has_value()) {
                    return core::Result<RuntimeEvent, RuntimeWorldError>::failure(
                        result.error());
                }
                return core::Result<RuntimeEvent, RuntimeWorldError>::success(
                    ObjectiveProgressChangedEvent{
                        value.objective,
                        ObjectiveProgress::failed});
            }
        },
        command);
}

}

core::Result<RuntimeCommandBatchResult, RuntimeCommandError>
RuntimeCommandProcessor::apply_atomic(
    RuntimeWorld& world,
    std::span<const RuntimeCommand> commands,
    std::size_t maximum_commands) const {
    if (commands.size() > maximum_commands) {
        return core::Result<RuntimeCommandBatchResult, RuntimeCommandError>::failure(
            {
                RuntimeCommandErrorCode::command_limit_exceeded,
                0,
                std::nullopt,
                "Runtime command batch exceeds the configured limit"
            });
    }

    RuntimeWorld candidate = world;
    RuntimeCommandBatchResult batch_result;
    batch_result.events.reserve(commands.size());
    for (std::size_t index = 0; index < commands.size(); ++index) {
        const auto result = apply_command(candidate, commands[index]);
        if (!result.has_value()) {
            return core::Result<
                RuntimeCommandBatchResult,
                RuntimeCommandError>::failure(
                {
                    RuntimeCommandErrorCode::command_failed,
                    index,
                    result.error(),
                    result.error().message
                });
        }
        batch_result.events.push_back(result.value());
    }

    world = std::move(candidate);
    return core::Result<RuntimeCommandBatchResult, RuntimeCommandError>::success(
        std::move(batch_result));
}

}
