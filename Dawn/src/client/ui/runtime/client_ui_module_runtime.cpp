#include "client_ui_module_runtime.h"

#include <string_view>

#include "../../../core/ui/modules/registry/ui_module_registry.h"
#include "../../../core/ui/modules/ui_module_descriptor.h"
#include "../mission_launch/mission_launch_panel.h"
#include "../movement/movement_panel.h"
#include "../camera/camera_panel.h"
#include "../player/player_panel.h"

namespace dawn::client::ui::runtime {
namespace {

/** Namespaced stable ID prevents Client modules from colliding with Server modules. */
constexpr std::string_view kMovementStableId = "client.movement";
constexpr std::string_view kPlayerStableId = "client.player";
/** Short menu label for the shared teleport and noclip page. */
constexpr std::string_view kMovementDisplayName = "Movement";
constexpr std::string_view kPlayerDisplayName = "Player";

core::ui::modules::registry::PageRegistration g_cameraPage;
core::ui::modules::registry::PageRegistration g_movementPage;
core::ui::modules::registry::PageRegistration g_playerPage;
core::ui::modules::registry::PageRegistration g_missionLaunchPage;

} // namespace

/** @return True when the visible Client pages own their registry slots. */
bool initialize() noexcept {
    const bool missionLaunchOwned = g_missionLaunchPage.acquire(
        core::ui::modules::Owner::client, "client.mission_launch", "Campaigns", &mission_launch::draw);
    const bool movementOwned = g_movementPage.acquire(
        core::ui::modules::Owner::client, kMovementStableId, kMovementDisplayName, &movement::draw);
    const bool playerOwned = g_playerPage.acquire(
        core::ui::modules::Owner::client, kPlayerStableId, kPlayerDisplayName, &player::draw);
    const bool cameraOwned = g_cameraPage.acquire(
        core::ui::modules::Owner::client, "client.camera", "Camera", &camera::draw);
    return movementOwned && playerOwned && missionLaunchOwned && cameraOwned;
}

/** Removes the Client module from the Core UI registry. */
void shutdown() noexcept {
    g_cameraPage.release();
    g_missionLaunchPage.release();
    g_playerPage.release();
    g_movementPage.release();
}

} // namespace dawn::client::ui::runtime
