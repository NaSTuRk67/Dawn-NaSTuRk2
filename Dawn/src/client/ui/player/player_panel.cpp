/** The player module's interface. Every control saves at once, so a change survives a restart. */

#include "player_panel.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <imgui.h>

#include "../../../core/ui/components/toggle/ui_toggle_component.h"
#include "../../../steam/runtime/runtime.h"
#include "../../player/player_settings_store.h"

namespace dawn::client::ui::player {
namespace {

constexpr int kPersonaStateChangeCallback = 304;
constexpr std::uint64_t kLocalSteamId = 0x0110000130AA9EC5ULL;
constexpr int kPersonaChangeName = 1;

struct PersonaStateChange {
    std::uint64_t steamId{};
    int changeFlags{};
    std::uint32_t padding{};
};
static_assert(sizeof(PersonaStateChange) == 16);

} // namespace

/** Draws the player module inside the active Core UI frame. */
void draw() noexcept {
    client::player::Settings settings = client::player::get();
    static std::array<char, 64> name{};
    static bool nameLoaded{};
    static std::array<char, 96> message{};
    if (!nameLoaded) {
        if (settings.personaName[0] != '\0') {
            name = settings.personaName;
        } else {
            (void)std::snprintf(name.data(), name.size(), "%s", "Player");
        }
        nameLoaded = true;
    }

    ImGui::TextUnformatted("Persona Name");
    ImGui::Separator();
    ImGui::TextWrapped("Changes the account name shown by Destiny after the next full restart.");
    ImGui::SetNextItemWidth(280.0F);
    (void)ImGui::InputText("##persona_name", name.data(), name.size());
    ImGui::SameLine();
    if (ImGui::Button("Save Name")) {
        bool valid = name[0] != '\0';
        for (const char* cursor = name.data(); valid && *cursor != '\0'; ++cursor) {
            const unsigned char value = static_cast<unsigned char>(*cursor);
            valid = value >= 0x20 && value <= 0x7E && value != '"' && value != '\\';
        }
        if (!valid) {
            (void)std::snprintf(message.data(), message.size(),
                                "Use 1-63 ordinary characters; quotes and backslashes are not allowed.");
        } else {
            settings.personaName = name;
            (void)client::player::publish(settings);
            const PersonaStateChange changed{kLocalSteamId, kPersonaChangeName, 0};
            const bool queued = steam::queue_callback(
                kPersonaStateChangeCallback, 0, &changed, sizeof(changed));
            (void)std::snprintf(message.data(), message.size(), "%s",
                                queued ? "Name saved; live persona refresh queued."
                                       : "Name saved; restart if this screen keeps the cached name.");
        }
    }
    if (message[0] != '\0') {
        ImGui::TextDisabled("%s", message.data());
    }
    ImGui::Spacing();

    ImGui::TextUnformatted("Infinite Ammo");
    ImGui::Separator();
    ImGui::TextWrapped("Keep every weapon's reserves full.");
    ImGui::Spacing();

    const bool changed = core::ui::components::toggle::control("Enabled##infinite_ammo",
                                                               settings.infiniteAmmoEnabled);
    if (changed) {
        (void)client::player::publish(settings);
    }
}

} // namespace dawn::client::ui::player
