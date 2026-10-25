/** Placeholder backend. Writes are refused and reads return nothing; see object_edit_backend.h. */

#include "object_edit_backend.h"

namespace dawn::tools::studio::object_edit {
namespace {

[[nodiscard]] std::string not_ported(const char* standsInFor) {
    return std::string{"NOT PORTED: "} + standsInFor;
}

} // namespace

bool UnportedStudioObjectEditBackend::can_edit() const noexcept {
    return false;
}

DescribeResult UnportedStudioObjectEditBackend::describe(const std::string& /*objectKey*/) {
    return DescribeResult{std::nullopt,
                          not_ported("row decoding (SceneEdits.Describe: package catalog and native row layouts)")};
}

std::optional<std::vector<Vec3>> UnportedStudioObjectEditBackend::hull_points(const std::string& /*objectKey*/) {
    return std::nullopt;
}

std::optional<ObjectEdit> UnportedStudioObjectEditBackend::saved_edit(const std::string& /*objectKey*/) {
    return std::nullopt;
}

std::string UnportedStudioObjectEditBackend::apply_edit(const ObjectEdit& /*edit*/, const std::string& /*label*/) {
    return not_ported("object edit writes and undo (StudioWorkspace.ApplyObjectEdits, MutateProject)");
}

std::string UnportedStudioObjectEditBackend::revert_edit(const std::string& /*objectKey*/) {
    return not_ported("object edit revert (StudioWorkspace.RevertObjectEdit)");
}

std::optional<std::string> UnportedStudioObjectEditBackend::object_name(const std::string& /*nameKey*/) {
    return std::nullopt;
}

std::string UnportedStudioObjectEditBackend::set_object_name(const std::string& /*nameKey*/,
                                                             const std::optional<std::string>& /*name*/) {
    return not_ported("project object names (StudioWorkspace.SetObjectName, StudioProject.ObjectNames)");
}

std::optional<CollisionPlacementInfo> UnportedStudioObjectEditBackend::collision_placement(std::int32_t /*index*/) {
    return std::nullopt;
}

} // namespace dawn::tools::studio::object_edit
