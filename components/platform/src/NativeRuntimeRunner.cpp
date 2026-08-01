#include <contract/platform/NativeRuntimeRunner.hpp>

#include <contract/collision/StaticCollisionWorld.hpp>
#include <contract/rendering/BgfxRenderer.hpp>
#include <contract/runtime/PlayerController.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
constexpr float pointer_look_radians_per_pixel{0.004F};

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
    std::uint32_t width{960};
    std::uint32_t height{540};
    bool closed{false};
    bool resized{false};
    bool wireframe{false};
    bool pointer_look_active{false};
    std::int32_t pointer_x{0};
    std::int32_t pointer_y{0};
    float pointer_yaw_delta{0.0F};
    float pointer_pitch_delta{0.0F};
    std::array<bool, 256> keys{};
};

void validate_paint(HWND window) {
    PAINTSTRUCT paint{};
    static_cast<void>(BeginPaint(window, &paint));
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
        if (state != nullptr && word < state->keys.size()) {
            state->keys[word] = true;
        }
        if (state != nullptr &&
            word == VK_F1 &&
            (static_cast<std::uint64_t>(data) &
             (std::uint64_t{1} << 30U)) == 0) {
            state->wireframe = !state->wireframe;
        }
        if (word == VK_ESCAPE) {
            static_cast<void>(DestroyWindow(window));
            return 0;
        }
        break;
    case WM_KEYUP:
        if (state != nullptr && word < state->keys.size()) {
            state->keys[word] = false;
        }
        return 0;
    case WM_RBUTTONDOWN:
        if (state != nullptr) {
            state->pointer_look_active = true;
            state->pointer_x = static_cast<std::int32_t>(
                static_cast<short>(LOWORD(data)));
            state->pointer_y = static_cast<std::int32_t>(
                static_cast<short>(HIWORD(data)));
            static_cast<void>(SetCapture(window));
        }
        return 0;
    case WM_RBUTTONUP:
        if (state != nullptr) {
            state->pointer_look_active = false;
        }
        if (GetCapture() == window) {
            static_cast<void>(ReleaseCapture());
        }
        return 0;
    case WM_MOUSEMOVE:
        if (state != nullptr && state->pointer_look_active) {
            const auto x = static_cast<std::int32_t>(
                static_cast<short>(LOWORD(data)));
            const auto y = static_cast<std::int32_t>(
                static_cast<short>(HIWORD(data)));
            state->pointer_yaw_delta +=
                static_cast<float>(x - state->pointer_x) *
                pointer_look_radians_per_pixel;
            state->pointer_pitch_delta -=
                static_cast<float>(y - state->pointer_y) *
                pointer_look_radians_per_pixel;
            state->pointer_x = x;
            state->pointer_y = y;
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (state != nullptr) {
            state->pointer_look_active = false;
        }
        return 0;
    case WM_KILLFOCUS:
        if (state != nullptr) {
            state->keys.fill(false);
            state->pointer_look_active = false;
            state->pointer_yaw_delta = 0.0F;
            state->pointer_pitch_delta = 0.0F;
        }
        if (GetCapture() == window) {
            static_cast<void>(ReleaseCapture());
        }
        return 0;
    case WM_SIZE:
        if (state != nullptr && word != SIZE_MINIMIZED) {
            state->width = static_cast<std::uint32_t>(
                LOWORD(data));
            state->height = static_cast<std::uint32_t>(
                HIWORD(data));
            state->resized =
                state->width > 0 && state->height > 0;
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        validate_paint(window);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, word, data);
}

bool key_down(
    const WindowState& state,
    std::size_t key) {
    return key < state.keys.size() && state.keys[key];
}

rendering::FreeCameraInput camera_input(
    WindowState& state) {
    rendering::FreeCameraInput input;
    input.forward =
        (key_down(state, 'W') ? 1.0F : 0.0F) -
        (key_down(state, 'S') ? 1.0F : 0.0F);
    input.right =
        (key_down(state, 'D') ? 1.0F : 0.0F) -
        (key_down(state, 'A') ? 1.0F : 0.0F);
    input.up =
        (key_down(state, 'E') ? 1.0F : 0.0F) -
        (key_down(state, 'Q') ? 1.0F : 0.0F);
    input.yaw =
        (key_down(state, VK_RIGHT) ? 1.0F : 0.0F) -
        (key_down(state, VK_LEFT) ? 1.0F : 0.0F);
    input.pitch =
        (key_down(state, VK_UP) ? 1.0F : 0.0F) -
        (key_down(state, VK_DOWN) ? 1.0F : 0.0F);
    input.yaw_delta = state.pointer_yaw_delta;
    input.pitch_delta = state.pointer_pitch_delta;
    state.pointer_yaw_delta = 0.0F;
    state.pointer_pitch_delta = 0.0F;
    input.fast =
        key_down(state, VK_SHIFT);
    return input;
}

runtime::PlayerInput player_input(
    const WindowState& state) {
    runtime::PlayerInput input;
    input.forward =
        (key_down(state, 'W') ? 1.0F : 0.0F) -
        (key_down(state, 'S') ? 1.0F : 0.0F);
    input.right =
        (key_down(state, 'D') ? 1.0F : 0.0F) -
        (key_down(state, 'A') ? 1.0F : 0.0F);
    input.sprint = key_down(state, VK_SHIFT);
    return input;
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
    WindowState state{host.observe()};
    auto created_window = create_runtime_window(state, visibility_);
    if (!created_window.has_value()) {
        return core::Result<void, runtime::RuntimeRunnerError>::failure(
            created_window.error());
    }
    const HWND window = created_window.value();
    rendering::BgfxRenderer renderer;
    auto renderer_initialized = renderer.initialize(
        window,
        state.width,
        state.height);
    if (!renderer_initialized.has_value()) {
        static_cast<void>(DestroyWindow(window));
        return platform_failure(
            renderer_initialized.error().message);
    }
    if (options.render_scene != nullptr) {
        auto uploaded = renderer.upload_scene(*options.render_scene);
        if (!uploaded.has_value()) {
            renderer.shutdown();
            static_cast<void>(DestroyWindow(window));
            return platform_failure(uploaded.error().message);
        }
    }
    if (options.player_model != nullptr) {
        auto uploaded = renderer.upload_player_model(
            *options.player_model);
        if (!uploaded.has_value()) {
            renderer.shutdown();
            static_cast<void>(DestroyWindow(window));
            return platform_failure(uploaded.error().message);
        }
    }
    std::optional<collision::StaticCollisionWorld> collision_world;
    if (options.collision_scene != nullptr &&
        !options.collision_scene->indices.empty()) {
        auto created = collision::StaticCollisionWorld::create(
            *options.collision_scene);
        if (!created.has_value()) {
            renderer.shutdown();
            static_cast<void>(DestroyWindow(window));
            return platform_failure(created.error().message);
        }
        collision_world.emplace(std::move(created.value()));
    }
    std::optional<runtime::PlayerController> player_controller;
    if (options.controlled_entity.has_value()) {
        if (collision_world.has_value()) {
            player_controller.emplace(
                *options.controlled_entity,
                *collision_world,
                collision::GroundedMotionConfig{
                    18.0F,
                    172.0F,
                    35.0F,
                    150.0F
                });
        } else {
            player_controller.emplace(*options.controlled_entity);
        }
    }
    const auto starting_tick = state.observation.completed_ticks;
    auto previous_time = std::chrono::steady_clock::now();
    rendering::CharacterAnimator character_animator;
    if (options.character_animation_timing.has_value()) {
        const auto& timing =
            options.character_animation_timing.value();
        auto configured = character_animator.configure(
            {
                timing.idle_duration_seconds,
                timing.walk_duration_seconds,
                timing.sprint_duration_seconds
            });
        if (!configured.has_value()) {
            renderer.shutdown();
            static_cast<void>(DestroyWindow(window));
            return platform_failure(configured.error().message);
        }
    }
    float player_camera_yaw = 0.0F;
    if (options.controlled_entity.has_value()) {
        const auto player = std::find_if(
            state.observation.entities.begin(),
            state.observation.entities.end(),
            [&options](
                const runtime::RuntimeEntityObservation& entity) {
                return entity.id == options.controlled_entity.value() &&
                    entity.enabled;
            });
        if (player != state.observation.entities.end()) {
            player_camera_yaw =
                rendering::camera_yaw_from_rotation(
                    player->transform.rotation);
        }
    }

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

        if (state.resized) {
            auto resized = renderer.resize(
                state.width,
                state.height);
            if (!resized.has_value()) {
                renderer.shutdown();
                static_cast<void>(DestroyWindow(window));
                return platform_failure(resized.error().message);
            }
            state.resized = false;
        }

        const auto current_time = std::chrono::steady_clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                current_time - previous_time);
        previous_time = current_time;
        std::vector<runtime::RuntimeCommand> commands;
        auto current_player_input = player_input(state);
        const auto current_camera_input = camera_input(state);
        const auto elapsed_seconds = (std::max)(
            std::chrono::duration<float>(elapsed).count(),
            0.000001F);
        player_camera_yaw = rendering::advance_camera_yaw(
            player_camera_yaw,
            current_camera_input.yaw,
            elapsed_seconds);
        if (std::isfinite(current_camera_input.yaw_delta)) {
            player_camera_yaw += current_camera_input.yaw_delta;
        }
        current_player_input.heading_radians =
            player_camera_yaw;
        auto animation_updated = character_animator.update(
            {
                current_player_input.forward,
                current_player_input.right,
                current_player_input.sprint
            },
            elapsed_seconds);
        if (!animation_updated.has_value()) {
            renderer.shutdown();
            if (IsWindow(window) != FALSE) {
                static_cast<void>(DestroyWindow(window));
            }
            return platform_failure(
                animation_updated.error().message);
        }
        if (player_controller.has_value()) {
            const auto movement = player_controller->update(
                state.observation,
                current_player_input,
                elapsed_seconds);
            if (!movement.has_value()) {
                renderer.shutdown();
                if (IsWindow(window) != FALSE) {
                    static_cast<void>(DestroyWindow(window));
                }
                return platform_failure(movement.error().message);
            }
            if (movement.value().has_value()) {
                commands.push_back(
                    std::move(movement.value().value()));
            }
        }
        auto frame = host.advance(elapsed, commands);
        if (!frame.has_value()) {
            renderer.shutdown();
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
        auto rendered = renderer.render(
            state.observation,
            current_camera_input,
            std::chrono::duration<float>(elapsed).count(),
            state.wireframe,
            character_animator.state());
        if (!rendered.has_value()) {
            renderer.shutdown();
            static_cast<void>(DestroyWindow(window));
            return platform_failure(rendered.error().message);
        }

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

    renderer.shutdown();
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
