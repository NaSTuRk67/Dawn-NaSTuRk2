#pragma once

namespace dawn::tools::parhelion {

/**
 * Draws the Parhelion weapon module inside the active Core UI frame. The first call starts the
 * item catalog load, and each frame after that shows the recipe being edited.
 */
void draw() noexcept;

/** Stops the catalog worker and drops the cached lists. The recipe text stays for the session. */
void shutdown() noexcept;

} // namespace dawn::tools::parhelion
