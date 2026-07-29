#pragma once

#include <contract/core/Result.hpp>

#include <string>
#include <string_view>

namespace contract::runtime {

enum class RuntimeLoadingPhase {
    source_data,
    runtime_setup,
    launching
};

struct RuntimeLoadingDisplayError {
    std::string message;
};

class IRuntimeLoadingDisplay {
public:
    virtual ~IRuntimeLoadingDisplay() = default;

    [[nodiscard]] virtual core::Result<
        void,
        RuntimeLoadingDisplayError>
    begin(std::string_view mission) = 0;

    [[nodiscard]] virtual core::Result<
        void,
        RuntimeLoadingDisplayError>
    update(RuntimeLoadingPhase phase) = 0;

    [[nodiscard]] virtual core::Result<
        void,
        RuntimeLoadingDisplayError>
    pump() = 0;

    [[nodiscard]] virtual core::Result<
        void,
        RuntimeLoadingDisplayError>
    complete() = 0;

    virtual void abort() noexcept = 0;
};

}
