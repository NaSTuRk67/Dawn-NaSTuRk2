#pragma once

/**
 * The seam between the object edit panel and the studio's scene and project data. The panel and the
 * rules in object_edit_model.h never touch package bytes, undo or the project file directly; those
 * live behind this interface. Dawn has no equivalent of the studio's scene loader, so the only
 * implementation here is UnportedStudioObjectEditBackend, which refuses every call.
 */

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "object_edit_model.h"

namespace dawn::tools::studio::object_edit {

/** Read-only facts of one placed collision shape, for the collision section. */
struct CollisionPlacementInfo final {
    std::string kind;
    std::string role;
    std::string source;
    /** Compound instance index, or -1 for a shape placed alone. */
    std::int32_t instance{-1};
    std::uint32_t triangles{};
    Vec3 worldPosition{};
    /** Bounds of the shape as drawn, in world space. */
    Vec3 drawnMinimum{};
    Vec3 drawnMaximum{};
};

/** The decoded row of a selection, or the reason it could not be decoded. */
struct DescribeResult final {
    std::optional<ObjectEditInfo> info;
    std::string error;
};

class ObjectEditBackend {
public:
    ObjectEditBackend() = default;
    ObjectEditBackend(const ObjectEditBackend&) = delete;
    ObjectEditBackend& operator=(const ObjectEditBackend&) = delete;
    ObjectEditBackend(ObjectEditBackend&&) = delete;
    ObjectEditBackend& operator=(ObjectEditBackend&&) = delete;
    virtual ~ObjectEditBackend() = default;

    /** Whether the project accepts edits. False keeps every edit control disabled. */
    [[nodiscard]] virtual bool can_edit() const noexcept = 0;

    /** Decodes the row an object key names. The key is ObjectEditTarget::key(). */
    [[nodiscard]] virtual DescribeResult describe(const std::string& objectKey) = 0;

    /** The authored hull points of a trigger volume. Nothing when they cannot be read. */
    [[nodiscard]] virtual std::optional<std::vector<Vec3>> hull_points(const std::string& objectKey) = 0;

    /** The edit this project already holds for the object, if any. */
    [[nodiscard]] virtual std::optional<ObjectEdit> saved_edit(const std::string& objectKey) = 0;

    /** Records one edit as one undo step. @return Empty on success, else the refusal. */
    [[nodiscard]] virtual std::string apply_edit(const ObjectEdit& edit, const std::string& label) = 0;

    /** Removes the saved edit of an object. @return Empty on success, else the refusal. */
    [[nodiscard]] virtual std::string revert_edit(const std::string& objectKey) = 0;

    /** The project's display name for a name key. The key is "edit:" plus the object key. */
    [[nodiscard]] virtual std::optional<std::string> object_name(const std::string& nameKey) = 0;

    /** Saves a display name. An empty optional removes it. @return Empty on success, else the refusal. */
    [[nodiscard]] virtual std::string set_object_name(const std::string& nameKey,
                                                      const std::optional<std::string>& name) = 0;

    /** The read-only facts of a loaded collision placement. Nothing when the index is not loaded. */
    [[nodiscard]] virtual std::optional<CollisionPlacementInfo> collision_placement(std::int32_t index) = 0;
};

/**
 * Placeholder backend. Writes are refused and reads return nothing. Each refusal starts with
 * "NOT PORTED:" and names the studio code it stands in for. Until a real backend exists, the panel
 * shows no editable object.
 */
class UnportedStudioObjectEditBackend final : public ObjectEditBackend {
public:
    [[nodiscard]] bool can_edit() const noexcept override;
    [[nodiscard]] DescribeResult describe(const std::string& objectKey) override;
    [[nodiscard]] std::optional<std::vector<Vec3>> hull_points(const std::string& objectKey) override;
    [[nodiscard]] std::optional<ObjectEdit> saved_edit(const std::string& objectKey) override;
    [[nodiscard]] std::string apply_edit(const ObjectEdit& edit, const std::string& label) override;
    [[nodiscard]] std::string revert_edit(const std::string& objectKey) override;
    [[nodiscard]] std::optional<std::string> object_name(const std::string& nameKey) override;
    [[nodiscard]] std::string set_object_name(const std::string& nameKey,
                                              const std::optional<std::string>& name) override;
    [[nodiscard]] std::optional<CollisionPlacementInfo> collision_placement(std::int32_t index) override;
};

} // namespace dawn::tools::studio::object_edit
