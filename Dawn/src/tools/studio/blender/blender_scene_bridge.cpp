/** STUB implementation. No Dawn scene source or sink is wired to the interchange yet. */

#include "blender_scene_bridge.h"

#include <utility>

namespace dawn::tools::studio::blender::bridge {

interchange::Result collect_scene_objects(interchange::Document& out) noexcept {
    out.objects.clear();
    return interchange::Result{interchange::Status::Unsupported,
                               "STUB: no Dawn scene source is wired; nothing was exported."};
}

interchange::Result apply_scene_objects(const interchange::Document& in) noexcept {
    (void)in;
    return interchange::Result{interchange::Status::Unsupported,
                               "STUB: no Dawn scene sink is wired; the file parsed but nothing was applied."};
}

} // namespace dawn::tools::studio::blender::bridge
