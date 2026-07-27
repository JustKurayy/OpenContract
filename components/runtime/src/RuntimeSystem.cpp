#include <contract/runtime/RuntimeSystem.hpp>

#include <utility>

namespace contract::runtime {

RuntimeCommandEmitter::RuntimeCommandEmitter(std::size_t maximum_commands)
    : maximum_commands_(maximum_commands) {}

core::Result<void, RuntimeSystemFailure> RuntimeCommandEmitter::emit(
    RuntimeCommand command) {
    if (commands_.size() >= maximum_commands_) {
        rejected_ = true;
        return core::Result<void, RuntimeSystemFailure>::failure(
            {
                RuntimeSystemFailureCode::command_limit_exceeded,
                "Runtime systems exceeded the per-tick command limit"
            });
    }
    commands_.push_back(std::move(command));
    return core::Result<void, RuntimeSystemFailure>::success();
}

core::Result<std::vector<RuntimeCommand>, RuntimeSystemError>
RuntimeSystemCoordinator::evaluate(
    const RuntimeWorld& world,
    const RuntimeSystemContext& context,
    std::span<const RuntimeSystem* const> systems,
    std::size_t maximum_commands) const {
    RuntimeCommandEmitter emitter(maximum_commands);
    for (std::size_t index = 0; index < systems.size(); ++index) {
        const auto* system = systems[index];
        if (system == nullptr) {
            return core::Result<
                std::vector<RuntimeCommand>,
                RuntimeSystemError>::failure(
                {
                    RuntimeSystemErrorCode::null_system,
                    index,
                    std::nullopt,
                    "Runtime system pointer cannot be null"
                });
        }

        const auto result = system->evaluate(world, context, emitter);
        if (emitter.rejected_) {
            const RuntimeSystemFailure failure{
                RuntimeSystemFailureCode::command_limit_exceeded,
                "Runtime systems exceeded the per-tick command limit"
            };
            return core::Result<
                std::vector<RuntimeCommand>,
                RuntimeSystemError>::failure(
                {
                    RuntimeSystemErrorCode::command_limit_exceeded,
                    index,
                    failure,
                    failure.message
                });
        }
        if (!result.has_value()) {
            return core::Result<
                std::vector<RuntimeCommand>,
                RuntimeSystemError>::failure(
                {
                    RuntimeSystemErrorCode::system_failed,
                    index,
                    result.error(),
                    result.error().message
                });
        }
    }

    return core::Result<
        std::vector<RuntimeCommand>,
        RuntimeSystemError>::success(std::move(emitter.commands_));
}

}
