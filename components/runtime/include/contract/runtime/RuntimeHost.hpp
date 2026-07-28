#pragma once

#include <contract/core/Result.hpp>
#include <contract/runtime/RuntimeObservation.hpp>
#include <contract/runtime/RuntimeSession.hpp>
#include <contract/runtime/RuntimeSystem.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace contract::runtime {

struct RuntimeFrame {
    RuntimeSessionAdvance advance;
    RuntimeObservation observation;
};

enum class RuntimeHostErrorCode {
    null_system,
    command_enqueue_failed,
    session_advance_failed
};

struct RuntimeHostError {
    RuntimeHostErrorCode code{RuntimeHostErrorCode::null_system};
    std::size_t command_index{0};
    std::size_t system_index{0};
    std::optional<RuntimeSessionError> session_error;
    std::string message;
};

class RuntimeHost {
public:
    RuntimeHost(const RuntimeHost&) = delete;
    RuntimeHost& operator=(const RuntimeHost&) = delete;
    RuntimeHost(RuntimeHost&&) noexcept = default;
    RuntimeHost& operator=(RuntimeHost&&) noexcept = default;

    [[nodiscard]] static core::Result<RuntimeHost, RuntimeHostError> create(
        RuntimeSession session,
        std::vector<std::unique_ptr<RuntimeSystem>> systems);

    [[nodiscard]] core::Result<RuntimeFrame, RuntimeHostError> advance(
        std::chrono::nanoseconds elapsed,
        std::span<const RuntimeCommand> commands = {});

    [[nodiscard]] RuntimeObservation observe() const;

private:
    RuntimeHost(
        RuntimeSession session,
        std::vector<std::unique_ptr<RuntimeSystem>> systems);

    RuntimeSession session_;
    std::vector<std::unique_ptr<RuntimeSystem>> systems_;
    std::vector<const RuntimeSystem*> system_order_;
};

}
