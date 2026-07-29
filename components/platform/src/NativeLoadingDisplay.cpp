#include <contract/platform/NativeLoadingDisplay.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace contract::platform {
namespace {

core::Result<void, runtime::RuntimeLoadingDisplayError>
loading_failure(std::string message) {
    return core::Result<
        void,
        runtime::RuntimeLoadingDisplayError>::failure(
        {std::move(message)});
}

#ifdef _WIN32

constexpr wchar_t loading_window_class[] =
    L"OpenContractLoadingWindow";
constexpr WORD arrow_cursor_resource_id{32512};

std::wstring widen_ascii(std::string_view value) {
    std::wstring converted;
    converted.reserve(value.size());
    for (const char character : value) {
        converted.push_back(
            static_cast<wchar_t>(
                static_cast<unsigned char>(character)));
    }
    return converted;
}

std::wstring phase_label(runtime::RuntimeLoadingPhase phase) {
    switch (phase) {
    case runtime::RuntimeLoadingPhase::source_data:
        return L"Reading mission data";
    case runtime::RuntimeLoadingPhase::runtime_setup:
        return L"Preparing mission runtime";
    case runtime::RuntimeLoadingPhase::launching:
        return L"Launching mission";
    }
    return L"Preparing mission";
}

#endif

}

struct NativeLoadingDisplayState {
#ifdef _WIN32
    HWND window{nullptr};
    std::wstring mission;
    std::wstring phase{L"Preparing mission"};
    bool closed{false};
#endif
};

#ifdef _WIN32
namespace {

void fill_rect(HDC device, const RECT& rectangle, COLORREF color) {
    const HBRUSH brush = CreateSolidBrush(color);
    if (brush != nullptr) {
        static_cast<void>(FillRect(device, &rectangle, brush));
        static_cast<void>(DeleteObject(brush));
    }
}

void paint_loading_window(
    HWND window,
    const NativeLoadingDisplayState& state) {
    PAINTSTRUCT paint{};
    const HDC device = BeginPaint(window, &paint);
    if (device == nullptr) {
        return;
    }

    RECT client{};
    static_cast<void>(GetClientRect(window, &client));
    fill_rect(device, client, RGB(14, 17, 22));

    static_cast<void>(SetBkMode(device, TRANSPARENT));
    static_cast<void>(SetTextColor(
        device,
        RGB(235, 238, 242)));

    RECT title{
        64,
        74,
        client.right - 64,
        126};
    static_cast<void>(DrawTextW(
        device,
        L"OpenContract",
        -1,
        &title,
        DT_LEFT | DT_TOP | DT_SINGLELINE));

    static_cast<void>(SetTextColor(
        device,
        RGB(244, 195, 67)));
    const std::wstring mission =
        state.mission.empty()
            ? L"Mission"
            : L"Mission " + state.mission;
    RECT mission_rect{
        64,
        178,
        client.right - 64,
        216};
    static_cast<void>(DrawTextW(
        device,
        mission.c_str(),
        -1,
        &mission_rect,
        DT_LEFT | DT_TOP | DT_SINGLELINE));

    static_cast<void>(SetTextColor(
        device,
        RGB(178, 186, 197)));
    RECT phase_rect{
        64,
        226,
        client.right - 64,
        264};
    static_cast<void>(DrawTextW(
        device,
        state.phase.c_str(),
        -1,
        &phase_rect,
        DT_LEFT | DT_TOP | DT_SINGLELINE));

    const LONG track_left = 64;
    const LONG track_right = client.right - 64;
    const LONG track_width = track_right - track_left;
    const RECT track{
        track_left,
        310,
        track_right,
        318};
    fill_rect(device, track, RGB(45, 51, 61));
    if (track_width > 0) {
        constexpr LONG indicator_width = 180;
        const auto elapsed =
            static_cast<std::uint64_t>(GetTickCount64());
        const LONG range = track_width + indicator_width;
        const LONG offset = static_cast<LONG>(
            (elapsed / 3U) %
            static_cast<std::uint64_t>(range));
        const LONG indicator_left =
            track_left + offset - indicator_width;
        RECT indicator{
            indicator_left,
            track.top,
            indicator_left + indicator_width,
            track.bottom};
        RECT clipped{};
        if (IntersectRect(&clipped, &indicator, &track) != 0) {
            fill_rect(device, clipped, RGB(244, 195, 67));
        }
    }

    static_cast<void>(EndPaint(window, &paint));
}

LRESULT CALLBACK loading_window_proc(
    HWND window,
    UINT message,
    WPARAM word,
    LPARAM data) {
    if (message == WM_NCCREATE) {
        auto* creation = reinterpret_cast<CREATESTRUCTW*>(data);
        auto* state = static_cast<NativeLoadingDisplayState*>(
            creation->lpCreateParams);
        static_cast<void>(SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state)));
    }

    auto* state = reinterpret_cast<NativeLoadingDisplayState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_CLOSE:
        static_cast<void>(DestroyWindow(window));
        return 0;
    case WM_DESTROY:
        if (state != nullptr) {
            state->closed = true;
            state->window = nullptr;
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (state != nullptr) {
            paint_loading_window(window, *state);
        } else {
            PAINTSTRUCT paint{};
            static_cast<void>(BeginPaint(window, &paint));
            static_cast<void>(EndPaint(window, &paint));
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, word, data);
}

core::Result<HWND, runtime::RuntimeLoadingDisplayError>
create_loading_window(NativeLoadingDisplayState& state) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (instance == nullptr) {
        return core::Result<
            HWND,
            runtime::RuntimeLoadingDisplayError>::failure(
            {"Could not obtain the native application instance"});
    }

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = loading_window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(
        nullptr,
        MAKEINTRESOURCEW(arrow_cursor_resource_id));
    window_class.lpszClassName = loading_window_class;
    if (RegisterClassExW(&window_class) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return core::Result<
            HWND,
            runtime::RuntimeLoadingDisplayError>::failure(
            {"Could not register the native loading window"});
    }

    const HWND window = CreateWindowExW(
        0,
        loading_window_class,
        L"OpenContract",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
            WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        960,
        540,
        nullptr,
        nullptr,
        instance,
        &state);
    if (window == nullptr) {
        return core::Result<
            HWND,
            runtime::RuntimeLoadingDisplayError>::failure(
            {"Could not create the native loading window"});
    }
    return core::Result<
        HWND,
        runtime::RuntimeLoadingDisplayError>::success(window);
}

}
#endif

NativeLoadingDisplay::NativeLoadingDisplay(
    NativeWindowVisibility visibility)
    : visibility_(visibility) {}

NativeLoadingDisplay::~NativeLoadingDisplay() {
    abort();
}

core::Result<void, runtime::RuntimeLoadingDisplayError>
NativeLoadingDisplay::begin(std::string_view mission) {
    abort();
#ifdef _WIN32
    state_ = std::make_unique<NativeLoadingDisplayState>();
    state_->mission = widen_ascii(mission);
    auto created = create_loading_window(*state_);
    if (!created.has_value()) {
        state_.reset();
        return loading_failure(created.error().message);
    }
    state_->window = created.value();
    if (visibility_ == NativeWindowVisibility::visible) {
        static_cast<void>(ShowWindow(state_->window, SW_SHOW));
    }
    static_cast<void>(UpdateWindow(state_->window));
    return core::Result<
        void,
        runtime::RuntimeLoadingDisplayError>::success();
#else
    static_cast<void>(mission);
    return loading_failure(
        "Native loading display is not available on this platform");
#endif
}

core::Result<void, runtime::RuntimeLoadingDisplayError>
NativeLoadingDisplay::update(runtime::RuntimeLoadingPhase phase) {
#ifdef _WIN32
    if (state_ == nullptr || state_->window == nullptr ||
        state_->closed) {
        return loading_failure(
            "The native loading display is not active");
    }
    state_->phase = phase_label(phase);
    static_cast<void>(InvalidateRect(
        state_->window,
        nullptr,
        FALSE));
    static_cast<void>(UpdateWindow(state_->window));
    return core::Result<
        void,
        runtime::RuntimeLoadingDisplayError>::success();
#else
    static_cast<void>(phase);
    return loading_failure(
        "Native loading display is not available on this platform");
#endif
}

core::Result<void, runtime::RuntimeLoadingDisplayError>
NativeLoadingDisplay::pump() {
#ifdef _WIN32
    if (state_ == nullptr || state_->closed) {
        return loading_failure(
            "The native loading display was closed");
    }
    MSG message{};
    while (PeekMessageW(
               &message,
               nullptr,
               0,
               0,
               PM_REMOVE) != 0) {
        static_cast<void>(TranslateMessage(&message));
        static_cast<void>(DispatchMessageW(&message));
    }
    if (state_->closed || state_->window == nullptr) {
        return loading_failure(
            "The native loading display was closed");
    }
    static_cast<void>(InvalidateRect(
        state_->window,
        nullptr,
        FALSE));
    static_cast<void>(UpdateWindow(state_->window));
    return core::Result<
        void,
        runtime::RuntimeLoadingDisplayError>::success();
#else
    return loading_failure(
        "Native loading display is not available on this platform");
#endif
}

core::Result<void, runtime::RuntimeLoadingDisplayError>
NativeLoadingDisplay::complete() {
#ifdef _WIN32
    if (state_ == nullptr) {
        return loading_failure(
            "The native loading display is not active");
    }
    if (state_->window != nullptr) {
        static_cast<void>(DestroyWindow(state_->window));
    }
    state_.reset();
    return core::Result<
        void,
        runtime::RuntimeLoadingDisplayError>::success();
#else
    return loading_failure(
        "Native loading display is not available on this platform");
#endif
}

void NativeLoadingDisplay::abort() noexcept {
#ifdef _WIN32
    if (state_ != nullptr && state_->window != nullptr) {
        static_cast<void>(DestroyWindow(state_->window));
    }
#endif
    state_.reset();
}

}
