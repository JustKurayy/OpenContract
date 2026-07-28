#include <contract/runtime/RuntimeHost.hpp>

#include <utility>

namespace contract::runtime {

RuntimeHost::RuntimeHost(
    RuntimeSession session,
    std::vector<std::unique_ptr<RuntimeSystem>> systems)
    : session_(std::move(session)),
      systems_(std::move(systems)) {
    system_order_.reserve(systems_.size());
    for (const auto& system : systems_) {
        system_order_.push_back(system.get());
    }
}

core::Result<RuntimeHost, RuntimeHostError> RuntimeHost::create(
    RuntimeSession session,
    std::vector<std::unique_ptr<RuntimeSystem>> systems) {
    for (std::size_t index = 0; index < systems.size(); ++index) {
        if (systems[index] == nullptr) {
            return core::Result<RuntimeHost, RuntimeHostError>::failure(
                {
                    RuntimeHostErrorCode::null_system,
                    0,
                    index,
                    std::nullopt,
                    "Runtime host system cannot be null"
                });
        }
    }
    return core::Result<RuntimeHost, RuntimeHostError>::success(
        RuntimeHost(std::move(session), std::move(systems)));
}

core::Result<RuntimeFrame, RuntimeHostError> RuntimeHost::advance(
    std::chrono::nanoseconds elapsed,
    std::span<const RuntimeCommand> commands) {
    auto candidate = session_;
    for (std::size_t index = 0; index < commands.size(); ++index) {
        const auto enqueued = candidate.enqueue(commands[index]);
        if (!enqueued.has_value()) {
            return core::Result<RuntimeFrame, RuntimeHostError>::failure(
                {
                    RuntimeHostErrorCode::command_enqueue_failed,
                    index,
                    0,
                    enqueued.error(),
                    enqueued.error().message
                });
        }
    }

    auto advanced = candidate.advance(elapsed, system_order_);
    if (!advanced.has_value()) {
        return core::Result<RuntimeFrame, RuntimeHostError>::failure(
            {
                RuntimeHostErrorCode::session_advance_failed,
                0,
                0,
                advanced.error(),
                advanced.error().message
            });
    }

    auto observation = candidate.observe();
    session_ = std::move(candidate);
    return core::Result<RuntimeFrame, RuntimeHostError>::success(
        {
            std::move(advanced.value()),
            std::move(observation)
        });
}

RuntimeObservation RuntimeHost::observe() const {
    return session_.observe();
}

}
