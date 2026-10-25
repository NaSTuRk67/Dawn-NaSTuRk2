/** The object edit inspector. Each control acts at once through the backend, as the studio's inspector does. */

#include "object_edit_panel.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

namespace dawn::tools::studio::object_edit {
namespace {

constexpr float kFullWidth = -1.0F;
constexpr std::size_t kMaximumNameLength = 200;

void copy_text(std::array<char, kMaximumNameLength + 1>& target, const std::string& text) noexcept {
    (void)std::snprintf(target.data(), target.size(), "%s", text.c_str());
}

[[nodiscard]] std::string trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

[[nodiscard]] std::array<float, 3> to_array(Vec3 value) noexcept {
    return {value.x, value.y, value.z};
}

[[nodiscard]] Vec3 to_vec(const std::array<float, 3>& value) noexcept {
    return Vec3{value[0], value[1], value[2]};
}

void draw_fact(const char* label, const std::string& value) noexcept {
    ImGui::TextDisabled("%s", label);
    ImGui::TextWrapped("%s", value.c_str());
}

/** Loads the editable fields from a decoded row. Rotation is shown in degrees. */
void load_fields(PanelState& state, const ObjectEditInfo& info) noexcept {
    state.position = to_array(info.position);
    state.rotationBase = to_array(degrees_from_quaternion(info.rotation));
    state.rotation = state.rotationBase;
    state.scale = info.scale;
    state.minimum = to_array(info.minimum);
    state.maximum = to_array(info.maximum);
}

/** Decodes the selected row once per selection, as the inspector caches its edit target. */
void ensure_described(ObjectEditBackend& backend, PanelState& state) noexcept {
    if (state.loadedKey == state.selectedKey) return;
    state.loadedKey = state.selectedKey;
    state.described.reset();
    DescribeResult result = backend.describe(state.selectedKey);
    state.describeError = std::move(result.error);
    if (result.info) {
        load_fields(state, *result.info);
        state.described = std::move(result.info);
    }
}

/** Reports the outcome of a built edit. A saved edit reloads the row, so the fields show what was stored. */
void apply_build(ObjectEditBackend& backend, PanelState& state, const EditBuild& build, const char* action) noexcept {
    if (build.refused()) {
        state.status = build.error;
        return;
    }
    if (!build.edit) {
        state.status = "No changes";
        return;
    }
    const std::string error = backend.apply_edit(*build.edit, action);
    if (!error.empty()) {
        state.status = error;
        return;
    }
    state.status = action;
    state.loadedKey.clear();
}

/** The object's name in this project. Saves when the field loses focus after an edit. */
void draw_common_properties(ObjectEditBackend& backend, PanelState& state, bool editable) noexcept {
    const std::string nameKey = "edit:" + state.selectedKey;
    if (state.nameDraftKey != nameKey) {
        state.nameDraftKey = nameKey;
        copy_text(state.nameDraft, backend.object_name(nameKey).value_or(std::string{}));
    }

    ImGui::TextUnformatted("Common properties");
    ImGui::Separator();
    ImGui::BeginDisabled(!editable);
    ImGui::SetNextItemWidth(kFullWidth);
    (void)ImGui::InputTextWithHint("##object-name", editable ? "Name in this project" : "Editing is unavailable",
                                   state.nameDraft.data(), state.nameDraft.size());
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        const std::string text{state.nameDraft.data()};
        const std::string current = backend.object_name(nameKey).value_or(std::string{});
        if (trimmed(text) != current) {
            const bool blank = trimmed(text).empty();
            const std::string error = backend.set_object_name(
                nameKey, blank ? std::nullopt : std::optional<std::string>{text});
            state.status = !error.empty() ? error : (blank ? "Object name removed" : "Object name saved");
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Names this placement in this project only; all its parts share the name. Clear it to remove.");
    }
    ImGui::EndDisabled();
}

void draw_transform(ObjectEditBackend& backend, PanelState& state, const ObjectEditInfo& info) noexcept {
    ImGui::TextDisabled("Position");
    ImGui::SetNextItemWidth(kFullWidth);
    (void)ImGui::InputFloat3("##position", state.position.data(), "%.3f");

    ImGui::TextDisabled("Rotation");
    ImGui::SetNextItemWidth(kFullWidth);
    (void)ImGui::InputFloat3("##rotation", state.rotation.data(), "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("XYZ degrees");

    if (info.canScale) {
        ImGui::TextDisabled("Scale");
        ImGui::SetNextItemWidth(kFullWidth);
        (void)ImGui::InputFloat("##scale", &state.scale, 0.0F, 0.0F, "%.4f");
    }

    if (ImGui::Button("Apply transform", ImVec2(kFullWidth, 0.0F))) {
        // An untouched rotation keeps the stored bytes exactly, so it is not re-encoded.
        std::optional<Quat> rotation;
        if (state.rotation != state.rotationBase) rotation = quaternion_from_degrees(to_vec(state.rotation));
        apply_build(backend, state, make_transform_edit(info, to_vec(state.position), rotation, state.scale),
                    "Saved object edit");
    }
}

void draw_bounds(ObjectEditBackend& backend, PanelState& state, const ObjectEditInfo& info) noexcept {
    ImGui::TextDisabled("Minimum");
    ImGui::SetNextItemWidth(kFullWidth);
    (void)ImGui::InputFloat3("##minimum", state.minimum.data(), "%.3f");

    ImGui::TextDisabled("Maximum");
    ImGui::SetNextItemWidth(kFullWidth);
    (void)ImGui::InputFloat3("##maximum", state.maximum.data(), "%.3f");

    if (ImGui::Button("Apply bounds", ImVec2(kFullWidth, 0.0F))) {
        const std::optional<std::vector<Vec3>> hull = backend.hull_points(state.selectedKey);
        if (!hull) {
            state.status = "Trigger hull points could not be read, so bounds cannot be checked.";
        } else {
            apply_build(backend, state,
                        make_bounds_edit(info, to_vec(state.minimum), to_vec(state.maximum), *hull),
                        "Saved object edit");
        }
    }
}

void draw_saved_edit(ObjectEditBackend& backend, PanelState& state) noexcept {
    const std::optional<ObjectEdit> saved = backend.saved_edit(state.selectedKey);
    if (!saved) return;
    ImGui::TextWrapped("Edited: %s", saved->summary().c_str());
    if (ImGui::Button("Revert object", ImVec2(kFullWidth, 0.0F))) {
        const std::string error = backend.revert_edit(state.selectedKey);
        state.status = error.empty() ? "Reverted object edit" : error;
        if (error.empty()) state.loadedKey.clear();
    }
}

void draw_placement(ObjectEditBackend& backend, PanelState& state, bool editable) noexcept {
    if (!state.described) {
        ImGui::TextDisabled("Placement read only");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", state.describeError.c_str());
        return;
    }
    const ObjectEditInfo& info = *state.described;
    if (!info.canTransform && !info.canBounds) {
        ImGui::TextDisabled("Placement read only");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", info.reason.c_str());
        return;
    }

    const std::string header = info.coordinateSpace + " placement##placement";
    if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::BeginDisabled(!editable);
    if (info.canTransform) draw_transform(backend, state, info);
    if (info.canBounds) draw_bounds(backend, state, info);
    draw_saved_edit(backend, state);
    ImGui::EndDisabled();
}

/** Read-only facts of a collision placement. Editing its parts needs the collision backend, which is not ported. */
void draw_collision(ObjectEditBackend& backend, PanelState& state) noexcept {
    const std::optional<CollisionPlacementInfo> placement = backend.collision_placement(state.selectedCollision);
    if (!placement) {
        ImGui::TextDisabled("This collision placement is not loaded.");
        return;
    }
    if (!ImGui::CollapsingHeader("Collision##collision", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::TextWrapped("%s", placement->kind.c_str());
    ImGui::TextDisabled("Collision");
    const Vec3 size{placement->drawnMaximum.x - placement->drawnMinimum.x,
                    placement->drawnMaximum.y - placement->drawnMinimum.y,
                    placement->drawnMaximum.z - placement->drawnMinimum.z};
    ImGui::TextDisabled("%s x %s x %s m", format_trimmed(size.x, 2).c_str(), format_trimmed(size.y, 2).c_str(),
                        format_trimmed(size.z, 2).c_str());

    draw_fact("Role", placement->role);
    draw_fact("Source", placement->source);
    draw_fact("Triangles", std::to_string(placement->triangles));
    if (placement->instance >= 0) draw_fact("Instance", std::to_string(placement->instance));
    const Vec3& at = placement->worldPosition;
    draw_fact("World position", format_trimmed(at.x, 3) + ", " + format_trimmed(at.y, 3) + ", "
        + format_trimmed(at.z, 3));
}

} // namespace

void select_object(PanelState& state, std::string objectKey) noexcept {
    state.selectedKey = std::move(objectKey);
    state.selectedCollision = -1;
    state.loadedKey.clear();
    state.described.reset();
    state.describeError.clear();
    state.status.clear();
}

void select_collision(PanelState& state, std::int32_t index) noexcept {
    state.selectedCollision = index;
    state.selectedKey.clear();
    state.loadedKey.clear();
    state.described.reset();
    state.describeError.clear();
    state.status.clear();
}

/** Draws the object edit inspector inside the active Core UI frame. */
void draw(ObjectEditBackend& backend, PanelState& state) noexcept {
    if (state.selectedKey.empty() && state.selectedCollision < 0) {
        ImGui::TextDisabled("Select an object to edit it.");
        return;
    }
    const bool editable = backend.can_edit();
    if (!editable) ImGui::TextDisabled("Editing is unavailable.");

    if (!state.selectedKey.empty()) {
        draw_common_properties(backend, state, editable);
        ensure_described(backend, state);
        draw_placement(backend, state, editable);
    }
    if (state.selectedCollision >= 0) draw_collision(backend, state);

    if (!state.status.empty()) ImGui::TextDisabled("%s", state.status.c_str());
}

} // namespace dawn::tools::studio::object_edit
