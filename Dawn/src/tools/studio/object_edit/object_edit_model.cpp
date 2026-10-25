/** Object edit rules. Each refusal string and check order follows the studio's edit rules. */

#include "object_edit_model.h"

#include <cmath>
#include <cstdio>
#include <utility>

namespace dawn::tools::studio::object_edit {
namespace {

constexpr float kUnitTolerance = 1e-3F;
constexpr float kMinimumRotationLengthSquared = 0.000001F;

[[nodiscard]] bool bounds_ordered(Vec3 minimum, Vec3 maximum) noexcept {
    return minimum.x <= maximum.x && minimum.y <= maximum.y && minimum.z <= maximum.z;
}

/** The inspector shows a row name only when it has one. */
[[nodiscard]] std::optional<std::string> label_of(const std::string& name) {
    if (name.empty()) return std::nullopt;
    return name;
}

} // namespace

const char* layout_name(ObjectEditLayout layout) noexcept {
    switch (layout) {
        case ObjectEditLayout::Placement: return "Placement";
        case ObjectEditLayout::SlotPlacement: return "SlotPlacement";
        case ObjectEditLayout::StaticInstance: return "StaticInstance";
        case ObjectEditLayout::Light: return "Light";
        case ObjectEditLayout::SpawnPoint: return "SpawnPoint";
        case ObjectEditLayout::TriggerVolume: return "TriggerVolume";
        case ObjectEditLayout::CollisionInstance: return "CollisionInstance";
        case ObjectEditLayout::DecoratorInstance: return "DecoratorInstance";
    }
    return "Unknown";
}

std::string format_trimmed(float value, int decimals) {
    char buffer[64]{};
    (void)std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, static_cast<double>(value));
    std::string text{buffer};
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
    }
    if (text == "-0") text = "0";
    return text;
}

std::string ObjectEditTarget::key() const {
    char buffer[96]{};
    (void)std::snprintf(buffer, sizeof(buffer), "%08X/%s/%X", static_cast<unsigned>(sourceTag),
                        layout_name(layout), static_cast<unsigned>(offset));
    return std::string{buffer};
}

bool ObjectEdit::has_values() const noexcept {
    return position.has_value() || rotation.has_value() || scale.has_value()
        || minimum.has_value() || maximum.has_value();
}

std::string ObjectEdit::summary() const {
    std::string text;
    const auto append = [&text](const std::string& part) {
        if (!text.empty()) text += ", ";
        text += part;
    };
    if (position) {
        append("position " + format_trimmed(position->x, 3) + " " + format_trimmed(position->y, 3) + " "
            + format_trimmed(position->z, 3));
    }
    if (rotation) append("rotation");
    if (scale) append("scale " + format_trimmed(*scale, 4));
    if (minimum || maximum) append("bounds");
    return text;
}

std::string validate_edit(const ObjectEdit& edit) {
    const ObjectEditTarget& target = edit.target;
    if (target.length <= 0 || target.offset < 0 || target.index < -1
        || static_cast<std::uint8_t>(target.layout) > static_cast<std::uint8_t>(ObjectEditLayout::DecoratorInstance)) {
        return "Invalid object edit target.";
    }
    const bool bounds = target.layout == ObjectEditLayout::TriggerVolume;
    if (bounds != (edit.kind == ObjectEditKind::TriggerBounds)) return "Object edit kind does not match its layout.";
    if (bounds && (!edit.minimum || !edit.maximum || edit.position || edit.rotation || edit.scale)) {
        return "Trigger bounds edits need a minimum and a maximum only.";
    }
    if (!bounds && (edit.minimum || edit.maximum)) return "Placement edits cannot set bounds.";
    if (!edit.has_values()) return "Object edit sets no value.";
    if ((edit.position && !is_finite(*edit.position)) || (edit.rotation && !is_finite(*edit.rotation))
        || (edit.minimum && !is_finite(*edit.minimum)) || (edit.maximum && !is_finite(*edit.maximum))) {
        return "Object edit values must be finite.";
    }
    if (edit.scale && (!std::isfinite(*edit.scale) || *edit.scale == 0.0F)) {
        return "Object edit scale must be finite and nonzero.";
    }
    if (edit.rotation && std::abs(length_squared(*edit.rotation) - 1.0F) > kUnitTolerance) {
        return "Object edit rotation must be a unit quaternion.";
    }
    if (edit.minimum && edit.maximum && !bounds_ordered(*edit.minimum, *edit.maximum)) {
        return "Object edit bounds are not ordered.";
    }
    return {};
}

std::optional<ObjectEdit> merge_edits(const std::optional<ObjectEdit>& earlier, const ObjectEdit& later,
                                      std::string& error) {
    error.clear();
    if (!earlier) return later;
    if (earlier->key() != later.key() || earlier->kind != later.kind) {
        error = "Only edits of the same object merge.";
        return std::nullopt;
    }
    ObjectEdit merged = later;
    if (!merged.name) merged.name = earlier->name;
    if (!merged.position) merged.position = earlier->position;
    if (!merged.rotation) merged.rotation = earlier->rotation;
    if (!merged.scale) merged.scale = earlier->scale;
    if (!merged.minimum) merged.minimum = earlier->minimum;
    if (!merged.maximum) merged.maximum = earlier->maximum;
    return merged;
}

ObjectEditInfo describe_stored(const ObjectEditTarget& target, const StoredRow& row, std::string coordinateSpace,
                               std::string name) {
    ObjectEditInfo info;
    info.target = target;
    info.name = std::move(name);
    info.coordinateSpace = std::move(coordinateSpace);
    info.row = row;
    if (target.layout == ObjectEditLayout::TriggerVolume) {
        info.canBounds = true;
        info.position = Vec3{(row.minimum.x + row.maximum.x) * 0.5F, (row.minimum.y + row.maximum.y) * 0.5F,
                             (row.minimum.z + row.maximum.z) * 0.5F};
        info.minimum = row.minimum;
        info.maximum = row.maximum;
        info.reason = "Trigger bounds metadata";
        return info;
    }
    info.canTransform = true;
    info.canScale = row.hasScale;
    info.position = row.position;
    info.rotation = row.rotation;
    info.scale = row.hasScale ? row.scale : 1.0F;
    return info;
}

EditBuild make_transform_edit(const ObjectEditInfo& info, Vec3 position, const std::optional<Quat>& rotation,
                              float scale) {
    EditBuild build;
    if (!info.target) {
        build.error = "This selection has no decoded target.";
        return build;
    }
    const ObjectEditTarget& target = *info.target;
    if (target.layout == ObjectEditLayout::TriggerVolume) {
        build.error = "Trigger hull transforms are not editable.";
        return build;
    }
    if (!is_finite(position) || (rotation && (!std::isfinite(length_squared(*rotation))
            || length_squared(*rotation) < kMinimumRotationLengthSquared))
        || !std::isfinite(scale) || scale == 0.0F) {
        build.error = "Placement needs finite values, a rotation direction and nonzero scale.";
        return build;
    }
    if (!info.row.hasScale && scale != 1.0F) {
        build.error = "This placement has no decoded scale field.";
        return build;
    }

    const StoredRow& row = info.row;
    ObjectEdit edit;
    edit.kind = ObjectEditKind::PlacementTransform;
    edit.target = target;
    edit.name = label_of(info.name);
    if (!(position == row.position)) edit.position = position;
    // Re-encoding an unchanged rotation would add float noise or flip q to -q.
    if (rotation) {
        const Quat unit = normalize(*rotation);
        if (!(unit == row.rotation)) edit.rotation = unit;
    }
    if (row.hasScale && scale != row.scale) edit.scale = scale;

    if (!edit.position && !edit.rotation && !edit.scale) return build;
    build.edit = std::move(edit);
    return build;
}

EditBuild make_bounds_edit(const ObjectEditInfo& info, Vec3 minimum, Vec3 maximum, const std::vector<Vec3>& hull) {
    EditBuild build;
    if (!info.target) {
        build.error = "This selection has no decoded target.";
        return build;
    }
    const ObjectEditTarget& target = *info.target;
    if (target.layout != ObjectEditLayout::TriggerVolume) {
        build.error = "This object has no decoded editable bounds.";
        return build;
    }
    if (!is_finite(minimum) || !is_finite(maximum) || !bounds_ordered(minimum, maximum)) {
        build.error = "Bounds need finite ordered corners.";
        return build;
    }
    const float extrusion = info.row.extrusion;
    if (!std::isfinite(extrusion) || extrusion < 0.0F) {
        build.error = "Trigger extrusion is invalid.";
        return build;
    }
    for (const Vec3& point : hull) {
        const Vec3 top{point.x, point.y, point.z + extrusion};
        if (!contains_inclusive(minimum, maximum, point) || !contains_inclusive(minimum, maximum, top)) {
            build.error = "Trigger bounds must contain the authored hull.";
            return build;
        }
    }
    if (minimum == info.row.minimum && maximum == info.row.maximum) return build;

    ObjectEdit edit;
    edit.kind = ObjectEditKind::TriggerBounds;
    edit.target = target;
    edit.name = label_of(info.name);
    edit.minimum = minimum;
    edit.maximum = maximum;
    build.edit = std::move(edit);
    return build;
}

} // namespace dawn::tools::studio::object_edit
