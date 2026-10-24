#include "camera_panel.h"

#include <Windows.h>
#include <array>
#include <cstdio>
#include <imgui.h>

#include "../../../core/ui/runtime/ui_visibility_runtime.h"
#include "../../../core/ui/scaling/dpi/ui_dpi_scaling.h"
#include "../../camera/camera_settings.h"
#include "../../hooks/camera/runtime.h"
#include "../../hooks/camera/clean_view.h"
#include "../../input/window_focus.h"

// Reuse the vendored Win32 backend's translation, including layout-dependent scan codes.
ImGuiKey ImGui_ImplWin32_KeyEventToImGuiKey(WPARAM word, LPARAM value);

namespace dawn::client::ui::camera {
namespace {
namespace settings = client::camera;
using settings::Action;
Action g_capturing{Action::count};
settings::KeyCapture g_capture{};
int g_lastFrame{-1};
bool g_reservedKey{};

[[nodiscard]] settings::KeyboardState keyboard() noexcept {
    settings::KeyboardState result{};
    for (int key = 8; key <= 254; ++key) {
        const UINT scan = MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC_EX);
        const LPARAM flags = static_cast<LPARAM>(((scan & 0xFFU) << 16)
            | ((scan & 0xFF00U) != 0 ? 1U << 24 : 0));
        const auto named = ImGui_ImplWin32_KeyEventToImGuiKey(static_cast<WPARAM>(key), flags);
        // ImGui queues key-down and key-up separately, so a tap between rendered frames
        // remains visible to the picker even after GetAsyncKeyState reports it released.
        // Only keys not represented by the backend need the old polling fallback.
        result[static_cast<std::size_t>(key)] = named != ImGuiKey_None
            ? ImGui::IsKeyDown(named) : (GetAsyncKeyState(key) & 0x8000) != 0;
        if (key == VK_RETURN) {
            // Windows uses one virtual key for the main and keypad Enter keys.
            result[VK_RETURN] = result[VK_RETURN] || ImGui::IsKeyDown(ImGuiKey_KeypadEnter);
        }
    }
    return result;
}

[[nodiscard]] std::array<char, 64> key_name(std::uint32_t key) noexcept {
    std::array<char, 64> result{};
    if (key == 0) {
        (void)std::snprintf(result.data(), result.size(), "Unbound");
        return result;
    }
    if (key >= VK_F1 && key <= VK_F24) {
        (void)std::snprintf(result.data(), result.size(), "F%u", static_cast<unsigned>(key - VK_F1 + 1));
        return result;
    }
    const UINT scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
    const LONG flags = static_cast<LONG>(((scan & 0xFFU) << 16) | ((scan & 0xFF00U) != 0 ? 1U << 24 : 0));
    std::array<wchar_t, 64> wide{};
    const int count = GetKeyNameTextW(flags, wide.data(), static_cast<int>(wide.size()));
    if (count <= 0 || WideCharToMultiByte(CP_UTF8, 0, wide.data(), count, result.data(),
                                         static_cast<int>(result.size() - 1), nullptr, nullptr) <= 0) {
        (void)std::snprintf(result.data(), result.size(), "Key 0x%02X", static_cast<unsigned>(key));
    }
    return result;
}

void draw_bindings() noexcept {
    constexpr std::array labels{"Cycle camera", "Rear third person", "Front third person",
                                "Remove HUD and player"};
    const auto ui = core::ui::runtime::snapshot();
    const int frame = ImGui::GetFrameCount();
    if (g_lastFrame != frame - 1 || !ui.visible || !input::game_focused()) {
        g_capturing = Action::count;
        g_reservedKey = false;
    }
    g_lastFrame = frame;
    if (g_capturing != Action::count) {
        const auto capture = g_capture.poll(keyboard(), ui.toggleVirtualKey);
        if (capture.kind == settings::CaptureKind::picked) {
            (void)settings::set_binding(g_capturing, capture.key);
            g_capturing = Action::count;
            g_reservedKey = false;
        } else if (capture.kind == settings::CaptureKind::cancelled) {
            g_capturing = Action::count;
            g_reservedKey = false;
        } else if (capture.kind == settings::CaptureKind::reserved) {
            g_reservedKey = true;
        }
    }

    ImGui::TextUnformatted("Key bindings");
    ImGui::Separator();
    ImGui::TextWrapped("Click a key to change it. Escape cancels; Backspace clears the binding. "
                       "If another camera control uses that key, their bindings swap.");
    const auto bindings = settings::bindings();
    if (ImGui::BeginTable("camera_bindings", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 150.0F * core::ui::scaling::dpi::current());
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < settings::kActionCount; ++i) {
            const auto action = static_cast<Action>(i);
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(labels[i]);
            ImGui::TableNextColumn();
            const auto name = key_name(bindings.keys[i]);
            const bool capturing = g_capturing == action;
            if (ImGui::Button(capturing ? "Press a key..." : name.data(), {-1.0F, 0.0F})) {
                g_capturing = capturing ? Action::count : action;
                g_capture.arm(keyboard());
                g_reservedKey = false;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (g_reservedKey) { ImGui::TextWrapped("That key opens NaSTuRk. Choose another key."); }
    if (ImGui::Button("Reset key bindings")) {
        settings::reset_bindings();
        g_capturing = Action::count;
        g_reservedKey = false;
    }
    ImGui::TextDisabled("Defaults: F5 cycle / F6 rear / F7 front / F8 hide");
    ImGui::TextWrapped("Bindings and camera mode save automatically.");
}
} // namespace

void draw() noexcept {
    ImGui::TextUnformatted("Camera mode");
    ImGui::Separator();
    const bool available = hooks::camera::is_installed();
    const auto selected = settings::mode();
    using Mode = settings::Mode;
    ImGui::BeginDisabled(!available);
    if (ImGui::RadioButton("Normal", selected == Mode::normal)) { settings::set_mode(Mode::normal); }
    if (ImGui::RadioButton("Third person (rear)", selected == Mode::rear)) { settings::set_mode(Mode::rear); }
    if (ImGui::RadioButton("Third person (front)", selected == Mode::front)) { settings::set_mode(Mode::front); }
    ImGui::EndDisabled();
    if (!available) { ImGui::TextWrapped("Camera controls are unavailable in this session."); }
    ImGui::TextWrapped("Cycle: Normal, Rear, Front, then Normal. "
                       "Press the rear or front key again to return to Normal.");
    ImGui::Spacing();

    bool clean = settings::camera_only();
    const bool cleanAvailable = hooks::camera::clean_view::installed();
    ImGui::BeginDisabled(!cleanAvailable);
    if (ImGui::Checkbox("Remove HUD and player", &clean)) { settings::set_camera_only(clean); }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Hide the game HUD and your character and weapon models. "
                       "Works in any camera mode; resets off each launch. NaSTuRk's menu stays available.");
    if (!cleanAvailable) { ImGui::TextWrapped("Camera-only visibility is unavailable in this session."); }
    ImGui::Spacing();
    ImGui::Spacing();
    draw_bindings();
}
} // namespace dawn::client::ui::camera
