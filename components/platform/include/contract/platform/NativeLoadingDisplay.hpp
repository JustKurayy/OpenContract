#pragma once

#include <contract/platform/NativeRuntimeRunner.hpp>
#include <contract/runtime/RuntimeLoadingDisplay.hpp>

#include <memory>

namespace contract::platform {

struct NativeLoadingDisplayState;

class NativeLoadingDisplay final
    : public runtime::IRuntimeLoadingDisplay {
public:
    explicit NativeLoadingDisplay(
        NativeWindowVisibility visibility =
            NativeWindowVisibility::visible);
    ~NativeLoadingDisplay() override;

    NativeLoadingDisplay(const NativeLoadingDisplay&) = delete;
    NativeLoadingDisplay& operator=(
        const NativeLoadingDisplay&) = delete;

    [[nodiscard]] core::Result<
        void,
        runtime::RuntimeLoadingDisplayError>
    begin(std::string_view mission) override;

    [[nodiscard]] core::Result<
        void,
        runtime::RuntimeLoadingDisplayError>
    update(runtime::RuntimeLoadingPhase phase) override;

    [[nodiscard]] core::Result<
        void,
        runtime::RuntimeLoadingDisplayError>
    pump() override;

    [[nodiscard]] core::Result<
        void,
        runtime::RuntimeLoadingDisplayError>
    complete() override;

    void abort() noexcept override;

private:
    NativeWindowVisibility visibility_;
    std::unique_ptr<NativeLoadingDisplayState> state_;
};

}
