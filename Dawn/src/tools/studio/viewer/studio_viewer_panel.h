#pragma once

namespace dawn::tools::studio::viewer {

/**
 * Draws the studio viewer inside the active tools frame. It is read-only: it never writes to a
 * package, a project or a mission script.
 */
void draw() noexcept;

/** Cancels a running load and drops the loaded snapshot. */
void shutdown() noexcept;

} // namespace dawn::tools::studio::viewer
