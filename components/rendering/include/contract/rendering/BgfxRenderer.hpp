#pragma once

#include <contract/core/Result.hpp>
#include <contract/runtime/RuntimeObservation.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace contract::rendering {

enum class RendererErrorCode {
    invalid_native_window,
    invalid_dimensions,
    initialization_failed,
    not_initialized
};

struct RendererError {
    RendererErrorCode code{RendererErrorCode::initialization_failed};
    std::string message;
};

class BgfxRenderer {
public:
    BgfxRenderer() = default;
    ~BgfxRenderer();

    BgfxRenderer(const BgfxRenderer&) = delete;
    BgfxRenderer& operator=(const BgfxRenderer&) = delete;
    BgfxRenderer(BgfxRenderer&&) = delete;
    BgfxRenderer& operator=(BgfxRenderer&&) = delete;

    [[nodiscard]] core::Result<void, RendererError> initialize(
        void* native_window,
        std::uint32_t width,
        std::uint32_t height);

    [[nodiscard]] core::Result<void, RendererError> resize(
        std::uint32_t width,
        std::uint32_t height);

    [[nodiscard]] core::Result<void, RendererError> render(
        const runtime::RuntimeObservation& observation);

    void shutdown() noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::string_view backend_name() const noexcept;

private:
    bool initialized_{false};
    std::uint32_t width_{0};
    std::uint32_t height_{0};
    std::string backend_name_;
};

}
