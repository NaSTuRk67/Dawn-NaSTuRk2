#pragma once

/** The object edit inspector: select an object, edit its transform, name and trigger bounds. */

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "object_edit_backend.h"
#include "object_edit_model.h"

namespace dawn::tools::studio::object_edit {

/** Panel state kept between frames: the selection, the loaded fields and the name draft. */
struct PanelState final {
    /** The selected object's key; empty when an object is not selected. */
    std::string selectedKey;
    /** The selected collision placement index; -1 when none is selected. */
    std::int32_t selectedCollision{-1};

    /** The key whose row the fields below were loaded from. Cleared to force a reload. */
    std::string loadedKey;
    std::optional<ObjectEditInfo> described;
    std::string describeError;

    /** Degrees, as the inspector shows them. rotationBase is the loaded value to compare against. */
    std::array<float, 3> position{};
    std::array<float, 3> rotation{};
    std::array<float, 3> rotationBase{};
    float scale{1.0F};
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};

    /** The name field's text, 200 characters at most plus the terminator. */
    std::array<char, 201> nameDraft{};
    std::string nameDraftKey;

    /** The last action's outcome, shown under the controls. */
    std::string status;
};

/** Selects an object. Clears any collision selection. */
void select_object(PanelState& state, std::string objectKey) noexcept;

/** Selects a collision placement. Clears any object selection. */
void select_collision(PanelState& state, std::int32_t index) noexcept;

/** Draws the object edit inspector inside the active Core UI frame. */
void draw(ObjectEditBackend& backend, PanelState& state) noexcept;

} // namespace dawn::tools::studio::object_edit
