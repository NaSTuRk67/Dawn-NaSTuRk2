#pragma once

/**
 * Bridge between the interchange document and Dawn's live scene.
 * Dawn has no scene-object accessor for this feature, so both functions are STUBS. They return
 * Status::Unsupported and do not touch any scene. Replace the bodies in blender_scene_bridge.cpp
 * when a real scene source and sink exist.
 */

#include "blender_interchange.h"

namespace dawn::tools::studio::blender::bridge {

/** STUB: collects the mesh objects of the current scene into out. Returns Unsupported. */
[[nodiscard]] interchange::Result collect_scene_objects(interchange::Document& out) noexcept;

/** STUB: applies imported mesh objects to the current scene. Returns Unsupported. */
[[nodiscard]] interchange::Result apply_scene_objects(const interchange::Document& in) noexcept;

} // namespace dawn::tools::studio::blender::bridge
