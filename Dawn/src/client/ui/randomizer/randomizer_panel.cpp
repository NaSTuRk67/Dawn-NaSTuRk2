// NOT COMPILED OR TESTED.
/**
 * The randomizer module's interface: a loadout roll and an armor stat adjuster. Nothing is changed
 * until a button is pressed. The panel talks to the account only through randomizer_bridge.h, which
 * is stubbed in this port.
 */

#include "randomizer_panel.h"

#include <cstddef>
#include <string>
#include <imgui.h>

#include "randomizer_bridge.h"
#include "randomizer_model.h"

namespace dawn::client::ui::randomizer {
namespace {

/** Width of each goal slider and of the Power field, in Core UI units. */
constexpr float kTargetSliderWidth = 220.0F;
constexpr float kPowerFieldWidth = 120.0F;
/** Drag step for the Power field, as the Core loadout randomizer uses. */
constexpr float kPowerDragStep = 10.0F;
/** Text color for a failed line, and for a goal the preview falls short of. */
const ImVec4 kFailedTextColor{1.0F, 0.45F, 0.45F, 1.0F};
const ImVec4 kShortTextColor{1.0F, 0.75F, 0.3F, 1.0F};

/** Panel state lives for the process, as the other Client panels keep theirs. */
PanelState& panel_state() noexcept {
    static PanelState state{};
    return state;
}

/** @return True when at least one goal is above zero. */
bool any_goal(const Stats& targets) noexcept {
    for (const int target : targets) {
        if (target > 0) {
            return true;
        }
    }
    return false;
}

/** Reads the preview again for the goals as they stand now. Called after any change to them. */
void refresh_preview(PanelState& panel) noexcept {
    panel.preview = bridge::preview_armor(panel.targets, panel.allowInventorySwaps);
    panel.feedback = Feedback{};
}

/** Draws one line of feedback, in the failed color when the last action did not go through. */
void draw_feedback(const Feedback& feedback) noexcept {
    if (feedback.text.empty()) {
        return;
    }
    if (feedback.failed) {
        ImGui::PushStyleColor(ImGuiCol_Text, kFailedTextColor);
        ImGui::TextWrapped("%s", feedback.text.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("%s", feedback.text.c_str());
    }
}

/** Draws the loadout roll: the sections to replace, the options, the Power field, and the button. */
void draw_loadout_section(PanelState& panel) noexcept {
    LoadoutOptions& options = panel.loadout;
    ImGui::TextUnformatted("Randomize Loadout");
    ImGui::Separator();
    ImGui::TextWrapped("Replaces the checked sections of this character with rolled gear.");
    ImGui::Spacing();

    const bool connected = bridge::is_connected();
    if (!connected) {
        ImGui::TextDisabled("%s", "Not connected in this client build.");
    }

    ImGui::BeginDisabled(!connected);
    (void)ImGui::Checkbox("Weapons##random_weapons", &options.weapons);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", "Equipped weapons and held weapon inventory");
    }
    ImGui::SameLine();
    (void)ImGui::Checkbox("Armor##random_armor", &options.armor);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", "Equipped armor and held armor inventory");
    }
    ImGui::SameLine();
    (void)ImGui::Checkbox("Equipment / Flair##random_flair", &options.equipmentFlair);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s",
                          "Ghosts, vehicles, ships, banners, emblems, emotes, and finishers");
    }
    ImGui::SameLine();
    (void)ImGui::Checkbox("Subclass##random_subclass", &options.subclass);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", "A class-compatible subclass with valid default abilities");
    }

    (void)ImGui::Checkbox("Replace Existing Inventory Items##random_replace",
                          &options.replaceHeldInventory);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", "Removes and regenerates held items in the checked sections.");
    }
    (void)ImGui::Checkbox("Keep Locked Items##random_keep_locked", &options.keepLockedItems);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", "Locked items are not randomized.");
    }

    ImGui::SetNextItemWidth(kPowerFieldWidth);
    (void)ImGui::DragInt("New Item Power##random_power", &panel.itemPower, kPowerDragStep, 0,
                         kItemPowerMaximum, "%d", ImGuiSliderFlags_AlwaysClamp);

    ImGui::BeginDisabled(!options.any());
    if (ImGui::Button("Randomize Loadout")) {
        const bridge::Outcome outcome = bridge::randomize_loadout(options, panel.itemPower);
        panel.feedback = Feedback{outcome.message, !outcome.ok};
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !options.any()) {
        ImGui::SetTooltip("%s", "Select at least one section");
    }
    ImGui::EndDisabled();

    ImGui::TextWrapped("Checked sections replace equipped items. Held items are kept unless "
                       "replacement is on. At most one Exotic weapon and one Exotic armor piece "
                       "are equipped.");
    draw_feedback(panel.feedback);
}

/** Draws one goal slider with its current and projected readouts. */
void draw_goal_row(PanelState& panel, std::size_t index, bool& changed) noexcept {
    ImGui::PushID(static_cast<int>(index));
    ImGui::TextUnformatted(kStatNames[index]);
    ImGui::SameLine(120.0F);
    ImGui::SetNextItemWidth(kTargetSliderWidth);
    if (ImGui::SliderInt("##goal", &panel.targets[index], 0, kStatTargetMaximum)) {
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Minimum overall %s. 0 ignores this stat. 100 is the useful cap.",
                          kStatNames[index]);
    }

    const bool preview = panel.preview.available;
    ImGui::SameLine();
    if (preview) {
        ImGui::TextDisabled("Current %d", panel.preview.current[index]);
    } else {
        ImGui::TextDisabled("Current -");
    }
    ImGui::SameLine();
    const bool short_ = preview && panel.targets[index] > panel.preview.projected[index];
    if (short_) {
        ImGui::PushStyleColor(ImGuiCol_Text, kShortTextColor);
    }
    if (preview) {
        ImGui::Text("Projected %d", panel.preview.projected[index]);
    } else {
        ImGui::TextDisabled("Projected -");
    }
    if (short_) {
        ImGui::PopStyleColor();
    }
    ImGui::PopID();
}

/** @return The one line of status the armor section shows when there is no feedback to show. */
std::string status_line(const PanelState& panel, bool connected) {
    if (!connected) {
        return "Loadout tools are not connected in this client build.";
    }
    if (!any_goal(panel.targets)) {
        return "Set one or more goals";
    }
    if (!panel.preview.available) {
        return "Equipped armor could not be read.";
    }
    if (panel.preview.exact) {
        return "Targets met";
    }
    unsigned missed = 0;
    for (const int shortfall : panel.preview.shortfalls) {
        if (shortfall > 0) {
            ++missed;
        }
    }
    return "Closest match - " + std::to_string(missed) + " goals short";
}

/** Draws the armor stat adjuster: goals, the swap option, the buttons, the status, and the summary. */
void draw_armor_section(PanelState& panel) noexcept {
    ImGui::TextUnformatted("Armor Stats");
    ImGui::Separator();
    ImGui::TextWrapped("Set target stats, then review before applying.");
    ImGui::Spacing();

    const bool connected = bridge::is_connected();
    bool changed = false;
    for (std::size_t index = 0; index < kStatCount; ++index) {
        draw_goal_row(panel, index, changed);
    }
    if (changed) {
        refresh_preview(panel);
    }
    ImGui::Spacing();

    ImGui::BeginDisabled(!connected);
    if (ImGui::Checkbox("Use Better Armor from Character Inventory##armor_swaps",
                        &panel.allowInventorySwaps)) {
        refresh_preview(panel);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s",
                          "The preview can equip unlocked armor from this character's inventory. "
                          "The equipped piece moves to inventory.");
    }
    ImGui::TextDisabled("Locked armor is always preserved");

    const bool hasGoals = any_goal(panel.targets);
    ImGui::BeginDisabled(!hasGoals);
    if (ImGui::Button("Clear")) {
        panel.targets = Stats{};
        panel.preview = ArmorPreview{};
        panel.feedback = Feedback{};
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    const bool canAdjust = connected && panel.preview.changes();
    ImGui::BeginDisabled(!canAdjust);
    if (ImGui::Button("Adjust Armor")) {
        const bridge::Outcome outcome =
            bridge::adjust_armor(panel.targets, panel.allowInventorySwaps);
        panel.feedback = Feedback{outcome.message, !outcome.ok};
        // The armor on now changed, so the preview is worked out again from the new state.
        panel.preview = bridge::preview_armor(panel.targets, panel.allowInventorySwaps);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !canAdjust) {
        ImGui::SetTooltip("%s", hasGoals ? "The preview does not require any armor changes"
                                         : "Set at least one goal first");
    }
    ImGui::EndDisabled();

    if (!panel.feedback.text.empty()) {
        draw_feedback(panel.feedback);
    } else {
        ImGui::TextDisabled("%s", status_line(panel, connected).c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Armor Preview");
    if (!panel.preview.available) {
        ImGui::TextDisabled("%s", "No preview is available.");
        return;
    }
    if (!panel.preview.changes()) {
        ImGui::TextDisabled("%s", "No armor changes");
    } else {
        ImGui::TextDisabled("%u pieces - %u swaps - %u masterworks - %u plugs",
                            panel.preview.pieces, panel.preview.swaps,
                            panel.preview.masterworks, panel.preview.plugs);
    }
}

} // namespace

/** Draws the randomizer module inside the active Core UI frame. */
void draw() noexcept {
    PanelState& panel = panel_state();
    draw_loadout_section(panel);
    ImGui::Spacing();
    ImGui::Spacing();
    draw_armor_section(panel);
}

} // namespace dawn::client::ui::randomizer
