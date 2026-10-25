#pragma once

/**
 * The object edit records and the rules that build them. The rules are pure: the caller passes
 * the stored row it read, and gets back an edit that holds only the values that differ from it.
 * Reading and writing the rows themselves is the backend's job (see object_edit_backend.h).
 */

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "object_edit_math.h"

namespace dawn::tools::studio::object_edit {

/** What an edit writes: a placement's transform, or a trigger volume's bounds. */
enum class ObjectEditKind : std::uint8_t {
    PlacementTransform,
    TriggerBounds,
};

/** The typed row an edit targets. The names match the studio's layout names. */
enum class ObjectEditLayout : std::uint8_t {
    Placement,
    SlotPlacement,
    StaticInstance,
    Light,
    SpawnPoint,
    TriggerVolume,
    CollisionInstance,
    DecoratorInstance,
};

[[nodiscard]] const char* layout_name(ObjectEditLayout layout) noexcept;

/** Formats like the studio's "0.###" text: fixed decimals with trailing zeros and the point removed. */
[[nodiscard]] std::string format_trimmed(float value, int decimals);

/** The row an edit targets. Its key merges every edit of one object. */
struct ObjectEditTarget final {
    std::uint32_t sourceTag{};
    ObjectEditLayout layout{ObjectEditLayout::Placement};
    std::int32_t offset{};
    std::int32_t length{};
    /** The array row for instances and lights; -1 when the layout has none. */
    std::int32_t index{-1};
    /** The placement identity or spawn name, as the layout defines it. */
    std::uint64_t identity{};
    /** Hash of the installed row this target was read from. Carried, not checked here. */
    std::string originalSha256;

    /** Formats as "SSSSSSSS/Layout/offset", with the offset in hex. */
    [[nodiscard]] std::string key() const;
};

/**
 * One edited object. Only set values are written; an unset value keeps the stored row exactly.
 * A rotation is a unit quaternion. The name is a display label only.
 */
struct ObjectEdit final {
    ObjectEditKind kind{ObjectEditKind::PlacementTransform};
    ObjectEditTarget target{};
    std::optional<std::string> name;
    std::optional<Vec3> position;
    std::optional<Quat> rotation;
    std::optional<float> scale;
    std::optional<Vec3> minimum;
    std::optional<Vec3> maximum;

    [[nodiscard]] std::string key() const { return target.key(); }
    [[nodiscard]] bool has_values() const noexcept;
    /** Short text for the changes list, such as "position 1 2 3, scale 3". */
    [[nodiscard]] std::string summary() const;
};

/** Checks what the edit can check without the row layouts. @return Empty when valid, else the reason. */
[[nodiscard]] std::string validate_edit(const ObjectEdit& edit);

/**
 * Later values win; the rest keep the earlier edit's values.
 * @return The merged edit, or nothing with error set when the two edit different objects.
 */
[[nodiscard]] std::optional<ObjectEdit> merge_edits(const std::optional<ObjectEdit>& earlier,
                                                    const ObjectEdit& later, std::string& error);

/** The stored values of one row, decoded. Trigger rows use minimum, maximum and extrusion. */
struct StoredRow final {
    /** True when the layout stores a scale field. Otherwise scale is 1 and cannot be edited. */
    bool hasScale{};
    Vec3 position{};
    Quat rotation{};
    float scale{1.0F};
    Vec3 minimum{};
    Vec3 maximum{};
    /** Trigger extrusion: how far the hull's top sits above its authored points. */
    float extrusion{};
};

/** What the inspector shows and allows for one selected row. */
struct ObjectEditInfo final {
    std::optional<ObjectEditTarget> target;
    /** The row's display name, when it has one. An edit carries it as its label. */
    std::string name;
    bool canTransform{};
    bool canScale{};
    bool canBounds{};
    Vec3 position{};
    Quat rotation{};
    float scale{1.0F};
    Vec3 minimum{};
    Vec3 maximum{};
    /** "World", "Local" or "Payload": the space the stored values are in. */
    std::string coordinateSpace;
    /** Why the row is read only, or a note about it. Empty for an ordinary placement. */
    std::string reason;
    StoredRow row{};
};

/**
 * Maps a decoded row to what the inspector can edit. Trigger volumes edit bounds only; every
 * other layout edits its transform, and its scale only when it stores one.
 */
[[nodiscard]] ObjectEditInfo describe_stored(const ObjectEditTarget& target, const StoredRow& row,
                                             std::string coordinateSpace, std::string name = {});

/** A built edit, or the reason it was refused. Refused and nothing-changed are different results. */
struct EditBuild final {
    /** Non-empty when the edit is refused. */
    std::string error;
    /** Empty when refused, and also when nothing changed. */
    std::optional<ObjectEdit> edit;

    [[nodiscard]] bool refused() const noexcept { return !error.empty(); }
};

/**
 * Records only the fields that differ from the stored row. An unset rotation keeps the stored bytes.
 * The rotation must be a unit direction; it is normalized, and a sign flip against the stored value is kept.
 */
[[nodiscard]] EditBuild make_transform_edit(const ObjectEditInfo& info, Vec3 position,
                                            const std::optional<Quat>& rotation, float scale);

/**
 * Trigger bounds stay separate from the authored hull points, which must all fit inside the new bounds.
 * @param hull The authored hull points of the trigger, read by the backend.
 */
[[nodiscard]] EditBuild make_bounds_edit(const ObjectEditInfo& info, Vec3 minimum, Vec3 maximum,
                                         const std::vector<Vec3>& hull);

} // namespace dawn::tools::studio::object_edit
