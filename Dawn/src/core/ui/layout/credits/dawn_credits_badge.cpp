#include "dawn_credits_badge.h"

#include <Windows.h>

#include <Shellapi.h>
#include <atomic>
#include <cstdint>
#include <imgui.h>
#include "../../modules/registry/ui_module_registry.h"

namespace dawn::core::ui::layout::credits {
namespace {

/** ShellExecuteW reserves values through 32 for failure details. */
constexpr std::intptr_t kFirstShellSuccessValue = 33;
/** Zero height asks Dear ImGui to use the current themed frame height. */
constexpr float kAutomaticButtonHeight = 0.0F;

std::atomic_int g_openPending{0};
modules::registry::PageRegistration g_page;

/**
 * Opens one URL through the Windows shell after a visible user click.
 * @param url Null-terminated URL from the badge.
 * @return True when Windows accepts the open action.
 */
[[nodiscard]] bool open_with_shell(const wchar_t* url) noexcept {
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<std::intptr_t>(result) >= kFirstShellSuccessValue;
}

} // namespace

/** Runs one queued source action through a caller-supplied bridge. */
bool dispatch_pending(OpenUrlAction openUrl) noexcept {
    if (openUrl == nullptr) return false;
    const int pending = g_openPending.exchange(0, std::memory_order_acq_rel);
    return pending != 0 && openUrl(pending == 2 ? kSundialUrl : kSourceUrl);
}

/** Queues one source action without leaving the active render call. */
void request_open() noexcept {
    request_open(Project::original);
}
void request_open(Project project) noexcept {
    g_openPending.store(project == Project::sundial ? 2 : 1, std::memory_order_release);
}

/** Cancels a queued source action at the layout lifecycle boundary. */
void cancel_pending() noexcept {
    g_openPending.store(0, std::memory_order_release);
}

/** Runs one queued source action through the Windows shell bridge. */
void dispatch_pending() noexcept {
    (void)dispatch_pending(&open_with_shell);
}

/** Draws the full-width badge and queues the source URL only after a click. */
void draw() noexcept {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5F);
    ImGui::TextUnformatted("Built on community contributions");
    ImGui::PopFont(); ImGui::Spacing();
    ImGui::TextWrapped("Dawn is possible because of the research, tools and contributions shared by these projects. Thank you.");
    ImGui::TextWrapped("NaSTuRk Edition: a custom build of Dawn with experimental Panoptes loadout features, item pictures and NaSTuRk branding.");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextUnformatted("Original project / stanuwu and contributors");
    ImGui::TextWrapped("Thank you for the foundation, game services, package research and original runtime that Dawn builds on.");
    if (ImGui::Button("Visit original project", {0, kAutomaticButtonHeight})) request_open(Project::original);
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextUnformatted("Sundial / KyleThmpsn and contributors");
    ImGui::TextWrapped("Thank you for the character and inventory editor, full perk selection, item artwork research, random loadouts and armor-stat tools that informed this integration.");
    if (ImGui::Button("Visit Sundial", {0, kAutomaticButtonHeight})) request_open(Project::sundial);
    ImGui::Spacing();
    ImGui::TextWrapped("Sundial adaptations are licensed under GPL-3.0-only. Source attribution and the license are included in vendor/sundial. Item names and artwork come from your installed game.");
}
bool initialize() noexcept {
    return g_page.acquire(modules::Owner::core, "core.credits", "Credits", &draw);
}
void shutdown() noexcept {
    g_page.release(); cancel_pending();
}

} // namespace dawn::core::ui::layout::credits
