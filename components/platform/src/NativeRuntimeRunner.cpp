#include <contract/platform/NativeRuntimeRunner.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace contract::platform {
namespace {

core::Result<void, runtime::RuntimeRunnerError> platform_failure(
    std::string message) {
    return core::Result<void, runtime::RuntimeRunnerError>::failure(
        {
            runtime::RuntimeRunnerErrorCode::platform_failed,
            std::move(message)
        });
}

#ifdef _WIN32

constexpr wchar_t runtime_window_class[] =
    L"OpenContractRuntimeWindow";
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

struct WindowState {
    runtime::RuntimeObservation observation;
    bool closed{false};
};

void paint_window(HWND window, const WindowState& state) {
    PAINTSTRUCT paint{};
    const HDC device = BeginPaint(window, &paint);
    RECT client{};
    static_cast<void>(GetClientRect(window, &client));
    const HBRUSH background = CreateSolidBrush(RGB(20, 23, 29));
    if (background != nullptr) {
        static_cast<void>(FillRect(device, &client, background));
        static_cast<void>(DeleteObject(background));
    }

    static_cast<void>(SetBkMode(device, TRANSPARENT));
    static_cast<void>(SetTextColor(device, RGB(226, 232, 240)));
    RECT text_area = client;
    text_area.left += 32;
    text_area.top += 28;
    text_area.right -= 32;
    text_area.bottom -= 28;

    std::wstring text =
        L"OpenContract\n\nOpen mod mission: ";
    text.append(widen_ascii(state.observation.mission.value()));
    text.append(L"\nMap: ");
    text.append(widen_ascii(state.observation.map.value()));
    text.append(L"\nSimulation tick: ");
    text.append(std::to_wstring(state.observation.completed_ticks));
    text.append(L"\nEntities: ");
    text.append(std::to_wstring(state.observation.entities.size()));
    text.append(L"\nObjectives: ");
    text.append(std::to_wstring(state.observation.objectives.size()));
    text.append(L"\n\nClose the window or press Escape to exit.");
    static_cast<void>(DrawTextW(
        device,
        text.c_str(),
        -1,
        &text_area,
        DT_LEFT | DT_TOP | DT_NOPREFIX));
    static_cast<void>(EndPaint(window, &paint));
}

LRESULT CALLBACK runtime_window_proc(
    HWND window,
    UINT message,
    WPARAM word,
    LPARAM data) {
    if (message == WM_NCCREATE) {
        auto* creation = reinterpret_cast<CREATESTRUCTW*>(data);
        auto* state = static_cast<WindowState*>(creation->lpCreateParams);
        static_cast<void>(SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state)));
    }

    auto* state = reinterpret_cast<WindowState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_CLOSE:
        static_cast<void>(DestroyWindow(window));
        return 0;
    case WM_DESTROY:
        if (state != nullptr) {
            state->closed = true;
        }
        return 0;
    case WM_KEYDOWN:
        if (word == VK_ESCAPE) {
            static_cast<void>(DestroyWindow(window));
            return 0;
        }
        break;
    case WM_PAINT:
        if (state != nullptr) {
            paint_window(window, *state);
            return 0;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, word, data);
}

core::Result<HWND, runtime::RuntimeRunnerError> create_runtime_window(
    WindowState& state,
    NativeWindowVisibility visibility) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (instance == nullptr) {
        return core::Result<HWND, runtime::RuntimeRunnerError>::failure(
            {
                runtime::RuntimeRunnerErrorCode::platform_failed,
                "Unable to obtain the application module handle"
            });
    }

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = runtime_window_proc;
    window_class.hInstance = instance;
    window_class.hCursor =
        LoadCursorW(
            nullptr,
            MAKEINTRESOURCEW(arrow_cursor_resource_id));
    window_class.lpszClassName = runtime_window_class;
    if (RegisterClassExW(&window_class) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return core::Result<HWND, runtime::RuntimeRunnerError>::failure(
            {
                runtime::RuntimeRunnerErrorCode::platform_failed,
                "Unable to register the native runtime window class"
            });
    }

    RECT bounds{0, 0, 960, 540};
    if (AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE) == FALSE) {
        return core::Result<HWND, runtime::RuntimeRunnerError>::failure(
            {
                runtime::RuntimeRunnerErrorCode::platform_failed,
                "Unable to calculate the native runtime window size"
            });
    }
    const std::wstring title =
        L"OpenContract - " +
        widen_ascii(state.observation.mission.value());
    const HWND window = CreateWindowExW(
        0,
        runtime_window_class,
        title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        nullptr,
        nullptr,
        instance,
        &state);
    if (window == nullptr) {
        return core::Result<HWND, runtime::RuntimeRunnerError>::failure(
            {
                runtime::RuntimeRunnerErrorCode::platform_failed,
                "Unable to create the native runtime window"
            });
    }

    if (visibility == NativeWindowVisibility::visible) {
        static_cast<void>(ShowWindow(window, SW_SHOWDEFAULT));
        static_cast<void>(UpdateWindow(window));
    }
    return core::Result<HWND, runtime::RuntimeRunnerError>::success(window);
}

#endif

}

NativeRuntimeRunner::NativeRuntimeRunner(
    NativeWindowVisibility visibility)
    : visibility_(visibility) {}

core::Result<void, runtime::RuntimeRunnerError> NativeRuntimeRunner::run(
    runtime::RuntimeHost& host,
    const runtime::RuntimeRunnerOptions& options) {
    if (options.maximum_frames.has_value() &&
        *options.maximum_frames == 0) {
        return platform_failure(
            "Maximum runtime frames must be positive");
    }

#ifdef _WIN32
    WindowState state{host.observe(), false};
    auto created_window = create_runtime_window(state, visibility_);
    if (!created_window.has_value()) {
        return core::Result<void, runtime::RuntimeRunnerError>::failure(
            created_window.error());
    }
    const HWND window = created_window.value();
    const auto starting_tick = state.observation.completed_ticks;
    auto previous_time = std::chrono::steady_clock::now();

    while (!state.closed) {
        MSG message{};
        while (PeekMessageW(
                   &message,
                   nullptr,
                   0,
                   0,
                   PM_REMOVE) != FALSE) {
            if (message.message == WM_QUIT) {
                state.closed = true;
                break;
            }
            static_cast<void>(TranslateMessage(&message));
            static_cast<void>(DispatchMessageW(&message));
        }
        if (state.closed) {
            break;
        }

        const auto current_time = std::chrono::steady_clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                current_time - previous_time);
        previous_time = current_time;
        auto frame = host.advance(elapsed, {});
        if (!frame.has_value()) {
            if (IsWindow(window) != FALSE) {
                static_cast<void>(DestroyWindow(window));
            }
            return core::Result<void, runtime::RuntimeRunnerError>::failure(
                {
                    runtime::RuntimeRunnerErrorCode::host_advance_failed,
                    frame.error().message
                });
        }
        state.observation = std::move(frame.value().observation);
        static_cast<void>(InvalidateRect(window, nullptr, FALSE));

        if (options.maximum_frames.has_value() &&
            state.observation.completed_ticks - starting_tick >=
                *options.maximum_frames) {
            break;
        }
        static_cast<void>(MsgWaitForMultipleObjectsEx(
            0,
            nullptr,
            1,
            QS_ALLINPUT,
            MWMO_INPUTAVAILABLE));
    }

    if (IsWindow(window) != FALSE) {
        static_cast<void>(DestroyWindow(window));
    }
    return core::Result<void, runtime::RuntimeRunnerError>::success();
#else
    static_cast<void>(host);
    return platform_failure(
        "Native runtime windows are supported only on Windows");
#endif
}

}
