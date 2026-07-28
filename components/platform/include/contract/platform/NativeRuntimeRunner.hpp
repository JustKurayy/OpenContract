#pragma once

#include <contract/runtime/RuntimeRunner.hpp>

namespace contract::platform {

enum class NativeWindowVisibility {
    visible,
    hidden
};

class NativeRuntimeRunner final : public runtime::IRuntimeRunner {
public:
    explicit NativeRuntimeRunner(
        NativeWindowVisibility visibility =
            NativeWindowVisibility::visible);

    [[nodiscard]] core::Result<void, runtime::RuntimeRunnerError> run(
        runtime::RuntimeHost& host,
        const runtime::RuntimeRunnerOptions& options) override;

private:
    NativeWindowVisibility visibility_{NativeWindowVisibility::visible};
};

}
