#include "server_ui_module_runtime.h"

#include <string_view>

#include "../../../core/ui/modules/registry/ui_module_registry.h"
#include "../../../core/ui/modules/ui_module_descriptor.h"
#include "../activity_override/activity_override_panel.h"
#include "../loadout/loadout_panel.h"
#include "../spawn/spawn_panel.h"

namespace dawn::server::ui::runtime {
namespace {

/** A namespaced stable ID keeps Server modules from clashing with Client modules. */
constexpr std::string_view kOverrideStableId = "server.activity_override";
/** Short menu label for the activity override page. */
constexpr std::string_view kOverrideDisplayName = "Activity";
constexpr std::string_view kSpawnStableId = "server.spawn";
constexpr std::string_view kSpawnDisplayName = "Spawn";
constexpr std::string_view kLoadoutStableId = "server.loadout";
constexpr std::string_view kLoadoutDisplayName = "Loadout";

core::ui::modules::registry::PageRegistration g_overridePage;
core::ui::modules::registry::PageRegistration g_spawnPage;
core::ui::modules::registry::PageRegistration g_loadoutPage;

} // namespace

/** @return True when the Server module owns its Core UI registry slot. */
bool initialize() noexcept {
    if (!g_overridePage.acquire(core::ui::modules::Owner::server,
                                kOverrideStableId,
                                kOverrideDisplayName,
                                &activity_override::draw)) {
        return false;
    }
    if (!g_spawnPage.acquire(core::ui::modules::Owner::server,
                             kSpawnStableId,
                             kSpawnDisplayName,
                             &spawn::draw)) {
        g_overridePage.release();
        return false;
    }
    // Loadout is an experimental convenience page, not a server dependency. A UI registration
    // problem must never roll back HTTP/BAP/gameplay initialization and surface as a content
    // startup failure. Activity and Spawn remain the required server pages above.
    (void)g_loadoutPage.acquire(core::ui::modules::Owner::server,
                                kLoadoutStableId,
                                kLoadoutDisplayName,
                                &loadout::draw);
    return true;
}

/** Removes the Server module from the Core UI registry. */
void shutdown() noexcept {
    g_loadoutPage.release(&loadout::shutdown);
    g_spawnPage.release();
    g_overridePage.release();
}

} // namespace dawn::server::ui::runtime
