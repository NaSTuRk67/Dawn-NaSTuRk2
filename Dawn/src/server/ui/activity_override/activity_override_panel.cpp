/**
 * The activity override module's interface. Nothing here is saved. The process starts with an
 * empty selection and the switch off, and the clear action puts it back.
 */

#include "activity_override_panel.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <imgui.h>
#include <span>
#include <string_view>

#include "../../../core/ui/components/picker/ui_picker_component.h"
#include "../../../core/ui/components/section/ui_section_component.h"
#include "../../../core/ui/components/toggle/ui_toggle_component.h"
#include "../../../state/activity/forced/activity_forced_destination.h"
#include "../../../state/activity/forced/prelaunch_profile.h"
#include "../../../state/activity/omega_ending.h"
#include "activity_override_lists.h"

namespace dawn::server::ui::activity_override {
namespace {

namespace forced = state::activity::forced;
namespace picker = core::ui::components::picker;

/** Preview shown by a picker with nothing chosen. */
constexpr char kUnset[] = "none";
/** Preview shown by a picker whose list is empty. */
constexpr char kEmptyList[] = "nothing to pick";
/** Spawn row that names no set, which forces the default one. */
constexpr char kDefaultSpawnRow[] = "none  (client picks)";
/** Room for the longest status line this module builds. */
constexpr std::size_t kStatusCapacity = 64;
/** Widest picker list this module draws, so no list is silently cut short. */
constexpr std::size_t kItemCapacity =
    (std::max)(state::build_data::scenarios::kDefinitionCapacity, kSpawnCapacity + 1);

/** Row each picker is highlighting. These follow the published selection, not the other way. */
std::size_t g_activityRow{kNoRow};
std::size_t g_bubbleRow{kNoRow};
std::size_t g_sliceRow{kNoRow};
std::size_t g_spawnRow{kNoRow};

/** @return The published selection's destination name as a bounded view. */
[[nodiscard]] std::string_view name_of(const forced::ForcedDestination& value) noexcept {
    return {value.packageName.data(), value.packageNameLength};
}

/**
 * Draws one labelled picker over a list of row labels.
 * @param id Stable Dear ImGui scope ID.
 * @param count Rows in use.
 * @param preview Text shown while the popup is closed.
 * @param row Highlighted row, updated on a pick.
 * @return True when a row was picked this frame.
 */
[[nodiscard]] bool row_picker(const char* id,
                              const char* caption,
                              const Label* labels,
                              std::size_t count,
                              const char* preview,
                              std::size_t& row) noexcept {
    ImGui::TextUnformatted(caption);
    std::array<picker::Item, kItemCapacity> items{};
    const std::size_t shown = (std::min)(count, items.size());
    for (std::size_t index = 0; index < shown; ++index) {
        items[index].label = labels[index].data();
    }
    return picker::control(
        id, shown != 0 ? preview : kEmptyList, std::span(items).first(shown), row);
}

/** Rebuilds every derived list. @param value Published selection. */
void follow_destination(const forced::ForcedDestination& value, Lists& rows) noexcept {
    refresh_destination(rows, name_of(value));
    g_bubbleRow = kNoRow;
    g_sliceRow = kNoRow;
    g_spawnRow = kNoRow;
}

/**
 * Selects a content-derived opening as one isolated mission profile.
 * The next native Chosen launch supplies the transport contract. The focused prelaunch owner
 * changes that contract before Destiny publishes it. The profile can name its arrival spawn set.
 * @return True when the extracted layout contains the selected opening.
 */
[[nodiscard]] bool apply_opening_profile(forced::ForcedDestination& value,
                                           Lists& rows,
                                           const forced::ForcedDestination& candidate) noexcept {
    const auto package = name_of(candidate);
    std::size_t activityRow = kNoRow;
    for (std::size_t index = 0; index < rows.activityCount; ++index) {
        if (std::string_view(rows.activities[index].data()) == package) {
            activityRow = index;
            break;
        }
    }
    if (activityRow == kNoRow) {
        return false;
    }

    refresh_destination(rows, package);

    std::size_t bubbleRow = kNoRow;
    for (std::size_t index = 0; index < rows.bubbleCount; ++index) {
        if (rows.bubbleOrdinals[index] == candidate.bubble) {
            bubbleRow = index;
            break;
        }
    }
    if (bubbleRow == kNoRow) {
        follow_destination(value, rows);
        return false;
    }
    refresh_bubble(rows, candidate.bubble);

    std::size_t sliceRow = kNoRow;
    for (std::size_t index = 0; index < rows.sliceCount; ++index) {
        if (rows.sliceValues[index] == candidate.sliceSet) {
            sliceRow = index;
            break;
        }
    }
    if (sliceRow == kNoRow) {
        follow_destination(value, rows);
        return false;
    }

    std::size_t spawnRow = 0;
    if (candidate.hasSpawnSetHash) {
        spawnRow = kNoRow;
        for (std::size_t index = 0; index < rows.spawnCount; ++index) {
            if (rows.spawnHashes[index] == candidate.spawnSetHash) {
                spawnRow = index + 1; // Row zero is the optional absent set.
                break;
            }
        }
        if (spawnRow == kNoRow) {
            follow_destination(value, rows);
            return false;
        }
    }

    value = candidate;
    g_activityRow = activityRow;
    g_bubbleRow = bubbleRow;
    g_sliceRow = sliceRow;
    g_spawnRow = spawnRow;
    return true;
}

/**
 * Draws the destination picker.
 * @param value Published selection, updated on a pick.
 * @return True when the selection changed.
 */
[[nodiscard]] bool draw_activity(forced::ForcedDestination& value, Lists& rows) noexcept {
    std::array<char, kLabelCapacity> preview{};
    const std::string_view name = name_of(value);
    (void)std::snprintf(
        preview.data(), preview.size(), "%.*s", static_cast<int>(name.size()), name.data());
    if (!row_picker("activity",
                    "Activity",
                    rows.activities.data(),
                    rows.activityCount,
                    name.empty() ? kUnset : preview.data(),
                    g_activityRow)) {
        return false;
    }
    const Label& picked = rows.activities[g_activityRow];
    const std::string_view text(picked.data());
    value.packageName = {};
    std::copy_n(
        text.begin(), (std::min)(text.size(), value.packageName.size()), value.packageName.begin());
    value.packageNameLength =
        static_cast<std::uint8_t>((std::min)(text.size(), value.packageName.size()));
    // The three lists below name rows of this destination only, so they go with it.
    value.hasBubble = false;
    value.bubble = 0;
    value.hasSliceSet = false;
    value.sliceSet = 0;
    value.hasSpawnSetHash = false;
    value.spawnSetHash = 0;
    follow_destination(value, rows);
    // Cinematic-only activities intentionally publish a scenario with no destination bubbles.
    // Preserve that authored shape instead of requiring a fake world arrival to enable them.
    value.bubbleless = rows.bubbleCount == 0;
    return true;
}

/**
 * Draws the bubble picker, which also seeds the slice set that bubble starts on.
 * @param value Published selection, updated on a pick.
 * @return True when the selection changed.
 */
[[nodiscard]] bool draw_bubble(forced::ForcedDestination& value, Lists& rows) noexcept {
    const char* const preview = value.hasBubble && g_bubbleRow < rows.bubbleCount
                                    ? rows.bubbles[g_bubbleRow].data()
                                    : kUnset;
    if (!row_picker(
            "bubble", "Bubble", rows.bubbles.data(), rows.bubbleCount, preview, g_bubbleRow)) {
        return false;
    }
    value.bubble = rows.bubbleOrdinals[g_bubbleRow];
    value.hasBubble = true;
    // The slice sets belong to the bubble, so the list is rebuilt for the one just picked.
    refresh_bubble(rows, value.bubble);
    // A bubble's first slice set is the one an ordinary arrival there uses, so picking a bubble
    // fills the slice set in instead of leaving the selection unfinished.
    g_sliceRow = kNoRow;
    value.hasSliceSet = false;
    value.sliceSet = 0;
    if (rows.sliceCount != 0) {
        g_sliceRow = 0;
        value.sliceSet = rows.sliceValues[0];
        value.hasSliceSet = true;
    }
    g_spawnRow = kNoRow;
    value.hasSpawnSetHash = false;
    value.spawnSetHash = 0;
    return true;
}

/**
 * Draws the slice-set picker.
 * @param value Published selection, updated on a pick.
 * @return True when the selection changed.
 */
[[nodiscard]] bool draw_slice(forced::ForcedDestination& value, Lists& rows) noexcept {
    const char* const preview =
        value.hasSliceSet && g_sliceRow < rows.sliceCount ? rows.slices[g_sliceRow].data() : kUnset;
    if (!row_picker(
            "slice", "Slice set", rows.slices.data(), rows.sliceCount, preview, g_sliceRow)) {
        return false;
    }
    value.sliceSet = rows.sliceValues[g_sliceRow];
    value.hasSliceSet = true;
    return true;
}

/**
 * Draws the spawn-set picker, whose first row names no set.
 * @param value Published selection, updated on a pick.
 * @return True when the selection changed.
 */
[[nodiscard]] bool draw_spawn(forced::ForcedDestination& value, Lists& rows) noexcept {
    // The leading row is what makes the spawn set optional. It forces the default set instead.
    static std::array<Label, kSpawnCapacity + 1> labels{};
    labels[0] = {};
    std::copy_n(
        std::string_view(kDefaultSpawnRow).begin(), sizeof kDefaultSpawnRow - 1, labels[0].begin());
    const std::size_t count = (std::min)(rows.spawnCount, labels.size() - 1) + 1;
    for (std::size_t index = 1; index < count; ++index) {
        labels[index] = rows.spawns[index - 1];
    }
    const char* const preview =
        value.hasSpawnSetHash && g_spawnRow < count ? labels[g_spawnRow].data() : labels[0].data();
    if (!row_picker("spawn", "Spawn set", labels.data(), count, preview, g_spawnRow)) {
        return false;
    }
    value.hasSpawnSetHash = g_spawnRow != 0;
    value.spawnSetHash = value.hasSpawnSetHash ? rows.spawnHashes[g_spawnRow - 1] : 0;
    return true;
}

/** Shows what the selection still needs. */
void draw_status(const forced::ForcedDestination& value, const Lists& rows) noexcept {
    if (forced::omega_completion_suspended()) {
        ImGui::TextWrapped("Override paused after Omega ended. Change the selection or switch Enabled off and on to force another launch.");
        return;
    }
    std::array<char, 128> debug{};
    (void)std::snprintf(debug.data(),
                        debug.size(),
                        "debug name=%u bubbleless=%u bubble=%u slice=%u rows=%zu selected=%u",
                        static_cast<unsigned>(value.packageNameLength),
                        value.bubbleless ? 1U : 0U,
                        value.hasBubble ? 1U : 0U,
                        value.hasSliceSet ? 1U : 0U,
                        rows.bubbleCount,
                        static_cast<unsigned>(rows.selected.nameLength));
    ImGui::TextDisabled("%s", debug.data());
    if (rows.activityCount == 0) {
        ImGui::TextUnformatted("waiting for the destination layouts to extract");
        return;
    }
    if (!value.enabled) {
        ImGui::TextDisabled("disabled, activities load normall");
        return;
    }
    if (!forced::active(value)) {
        ImGui::TextUnformatted("incomplete, select valid activity");
        return;
    }
    if (value.bubbleless) {
        ImGui::TextUnformatted("active, launching a cinematic-only activity");
        return;
    }
    ImGui::TextUnformatted(value.hasSpawnSetHash ? "active, forcing the chosen spawn set"
                                                 : "active, the client picks its own spawn");
    if (forced::prelaunch::configured(value) != nullptr) {
        ImGui::TextUnformatted("prelaunch armed; start Chosen from the Director");
    }
    if (rows.spawnUnavailable) {
        ImGui::TextDisabled("this destination's spawn sets could not be listed");
    }
    if (!rows.spawnNarrowed) {
        ImGui::TextDisabled("currently showing spawn sets for all bubbles");
        return;
    }
    std::array<char, kStatusCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "spawn sets of this bubble, %zu of the map's hidden",
                                      rows.spawnHidden);
    if (written > 0) {
        ImGui::TextDisabled("%s", line.data());
    }
    // A marked row is the failure the operator cannot see otherwise. The bubble is right, the
    // package is not loaded, and the client then spawns nothing at all.
    if (rows.spawnForeign != 0) {
        std::array<char, kStatusCapacity> foreign{};
        const int count = std::snprintf(foreign.data(),
                                        foreign.size(),
                                        "%zu in a package this destination does not load",
                                        rows.spawnForeign);
        if (count > 0) {
            ImGui::TextDisabled("%s", foreign.data());
        }
    }
}

} // namespace

/** Draws the activity override module inside the active Core UI frame. */
void draw() noexcept {
    Lists& rows = lists();
    refresh_activities(rows);
    forced::ForcedDestination value{};
    // The panel reads the raw stored selection, not the effective one, so it stays clean while the
    // built-in Omega default is in effect. The user's own picks still show and still take priority.
    forced::stored(value);
    // The rows follow whatever State holds, so a selection cleared elsewhere clears the lists.
    if (name_of(value).empty() && rows.bubbleCount != 0) {
        follow_destination(value, rows);
        g_activityRow = kNoRow;
    }

    core::ui::components::section::header("Activity override",
                                          "Forces every load to redirect to these values.");

    bool changed = core::ui::components::toggle::control("Enabled", value.enabled);
    // Re-derive this from the published layout every frame. It keeps a cached UI selection and
    // State synchronized across a destination refresh as well as on the frame it was picked.
    const bool bubbleless = value.packageNameLength != 0 && rows.bubbleCount == 0;
    if (value.bubbleless != bubbleless) {
        value.bubbleless = bubbleless;
        changed = true;
    }
    if (ImGui::Button("Towerfall opening")) {
        changed = apply_opening_profile(value, rows, forced::profiles::kTowerfallOpening) || changed;
    }
    ImGui::SameLine();
    if (ImGui::Button("A Deadly Trial opening")) {
        changed = apply_opening_profile(value, rows, forced::profiles::kDeadlyTrialOpening) || changed;
    }
    if (ImGui::Button("New Light / Launchpad opening")) {
        changed = apply_opening_profile(value, rows, forced::profiles::kLaunchpadOpening) || changed;
    }
    if (ImGui::Button("Deep Storage opening")) {
        changed = apply_opening_profile(value, rows, forced::profiles::kDeepStorageOpening) || changed;
    }
    if (ImGui::Button("Hijacked opening")) {
        changed = apply_opening_profile(value, rows, forced::profiles::kHijackedOpening) || changed;
    }
    if (ImGui::Button("Beyond Infinity opening")) {
        changed = apply_opening_profile(value, rows, forced::profiles::kBeyondInfinityOpening) || changed;
    }
    if (ImGui::Button("Gateway opening")) {
        changed = apply_opening_profile(value, rows, forced::profiles::kGatewayOpening) || changed;
    }
    ImGui::SameLine();
    if (ImGui::Button("Tree of Probabilities opening")) {
        changed = apply_opening_profile(value, rows, forced::profiles::kStrikePactOpening) || changed;
    }
    if (ImGui::Button("Haunted Forest opening")) {
        changed = apply_opening_profile(value, rows, forced::profiles::kHauntedForestOpening) || changed;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        forced::clear();
        value = {};
        g_activityRow = kNoRow;
        follow_destination(value, rows);
        changed = false;
    }

    ImGui::Spacing();
    changed = draw_activity(value, rows) || changed;
    ImGui::Spacing();
    changed = draw_bubble(value, rows) || changed;
    ImGui::Spacing();
    changed = draw_slice(value, rows) || changed;
    ImGui::Spacing();
    changed = draw_spawn(value, rows) || changed;

    ImGui::Spacing();
    ImGui::Separator();
    draw_status(value, rows);
    if (changed && !forced::publish(value)) {
        ImGui::TextUnformatted("value out of range, not applied");
    }
    ImGui::Separator();
    const bool canPreview = state::activity::omega_ending::preview_available();
    ImGui::BeginDisabled(!canPreview);
    if (ImGui::Button("Test Omega ending")) {
        (void)state::activity::omega_ending::request_preview();
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Available at Omega's opening Lighthouse before entering the Forest. Plays the ending and tests the Mercury return without replaying the fight. This preview does not complete the quest.");
}

} // namespace dawn::server::ui::activity_override
