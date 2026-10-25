#pragma once

#include <string>

/**
 * Stubs for Sunrise features that Dawn does not have yet. Each one does nothing, and each says so in its name
 * and its comment. NOTES.md lists them with the reason.
 */
namespace dawn::tools::studio::mission_graph::stubs {

/**
 * STUB. Sunrise keeps each script's node positions in a layout file beside the script (MissionLayoutStore).
 * Dawn has no equivalent yet, so nothing is written and positions last only for this session.
 * @param path The script the layout belongs to.
 * @return Always false, meaning nothing was saved.
 */
inline bool save_layout(const std::string& path) noexcept {
    (void)path;
    return false;
}

/**
 * STUB. Sunrise checks each typed API call against the mission SDK schema (MissionApiSchema). Dawn has no
 * port of that schema, so calls are not checked.
 * @return Always false.
 */
inline bool typed_api_checks_available() noexcept { return false; }

} // namespace dawn::tools::studio::mission_graph::stubs
