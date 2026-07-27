#pragma once

#include <contract/core/Result.hpp>
#include <contract/runtime/RuntimeCommand.hpp>
#include <contract/runtime/RuntimeWorld.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace contract::runtime {

struct RuntimeSystemContext {
    std::uint64_t tick{0};
    std::chrono::nanoseconds step{0};
};

enum class RuntimeSystemFailureCode {
    command_limit_exceeded,
    evaluation_failed
};

struct RuntimeSystemFailure {
    RuntimeSystemFailureCode code{
        RuntimeSystemFailureCode::evaluation_failed};
    std::string message;
};

class RuntimeCommandEmitter {
public:
    [[nodiscard]] core::Result<void, RuntimeSystemFailure> emit(
        RuntimeCommand command);

private:
    friend class RuntimeSystemCoordinator;

    explicit RuntimeCommandEmitter(std::size_t maximum_commands);

    std::size_t maximum_commands_{0};
    bool rejected_{false};
    std::vector<RuntimeCommand> commands_;
};

class RuntimeSystem {
public:
    virtual ~RuntimeSystem() = default;

    [[nodiscard]] virtual core::Result<void, RuntimeSystemFailure> evaluate(
        const RuntimeWorld& world,
        const RuntimeSystemContext& context,
        RuntimeCommandEmitter& emitter) const = 0;
};

enum class RuntimeSystemErrorCode {
    null_system,
    command_limit_exceeded,
    system_failed
};

struct RuntimeSystemError {
    RuntimeSystemErrorCode code{RuntimeSystemErrorCode::null_system};
    std::size_t system_index{0};
    std::optional<RuntimeSystemFailure> failure;
    std::string message;
};

class RuntimeSystemCoordinator {
public:
    [[nodiscard]] core::Result<
        std::vector<RuntimeCommand>,
        RuntimeSystemError>
    evaluate(
        const RuntimeWorld& world,
        const RuntimeSystemContext& context,
        std::span<const RuntimeSystem* const> systems,
        std::size_t maximum_commands) const;
};

}
