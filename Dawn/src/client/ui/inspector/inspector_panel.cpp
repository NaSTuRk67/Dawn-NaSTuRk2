/** The item and perk definition inspector: search and browse, then a read-only detail view. */

#include "inspector_panel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <imgui.h>
#include <string_view>
#include <vector>

#include "../../../core/ui/components/components.h"
#include "../../../core/ui/scaling/dpi/ui_dpi_scaling.h"
#include "inspector_catalog.h"
#include "inspector_stubs.h"

namespace dawn::client::ui::inspector {
namespace {

constexpr float kTierWidth = 150.0F;
constexpr float kFactLabelWidth = 190.0F;
/** Text cell for one formatted value. Every value in this module is shorter than this. */
using Text = std::array<char, 160>;

/** Formats one value into a cell. Callers pass the result straight into a row, in the same expression. */
template <typename... Args>
[[nodiscard]] Text fmt(const char* format, Args... args) noexcept {
    Text out{};
    if constexpr (sizeof...(Args) == 0) {
        // A literal with no arguments is copied, never passed to snprintf as a format.
        (void)std::snprintf(out.data(), out.size(), "%s", format);
    } else {
        (void)std::snprintf(out.data(), out.size(), format, args...);
    }
    return out;
}

/** "Set N", or "None" for the unavailable sentinel. */
[[nodiscard]] Text set_text(std::uint16_t setIndex) noexcept {
    return setIndex == build::items::kUnavailableMaterialRequirementSetIndex
        ? fmt("None") : fmt("Set %u", static_cast<unsigned>(setIndex));
}

/** "%u", or "None" for the unavailable sentinel. */
[[nodiscard]] Text index_text(std::uint16_t value, std::uint16_t unavailable) noexcept {
    return value == unavailable ? fmt("None") : fmt("%u", static_cast<unsigned>(value));
}

/** Clamps a stored count to the capacity of the array it counts. */
[[nodiscard]] std::size_t clamp_count(std::size_t count, std::size_t capacity) noexcept {
    return (count < capacity) ? count : capacity;
}

/** A two-column field table. The table is open only while this object lives. */
class Facts {
public:
    explicit Facts(const char* id) noexcept
        : open_(ImGui::BeginTable(id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        if (open_) {
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed,
                                    core::ui::scaling::dpi::pixels(kFactLabelWidth));
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        }
    }
    ~Facts() noexcept {
        if (open_) {
            ImGui::EndTable();
        }
    }
    Facts(const Facts&) = delete;
    Facts& operator=(const Facts&) = delete;
    Facts(Facts&&) = delete;
    Facts& operator=(Facts&&) = delete;

    void row(const char* label, const Text& value) const noexcept {
        if (!open_) {
            return;
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(value.data());
    }

private:
    bool open_{};
};

// Page state. Each frame reads it; only this module's own draw calls write it.
Snapshot g_snapshot{};
bool g_loaded{};
Filter g_filter{};
Filter g_applied{};
bool g_appliedValid{};
std::vector<std::uint32_t> g_rows{};
bool g_hasSelection{};
std::uint16_t g_selected{};
bool g_openDetail{};

void ensure_loaded() noexcept {
    if (g_loaded) {
        return;
    }
    g_loaded = load(g_snapshot);
    g_appliedValid = false;
}

void reload() noexcept {
    g_loaded = false;
    ensure_loaded();
}

/** Rebuilds the result rows only when the filter changed since the last build. */
void refresh_rows() noexcept {
    if (g_appliedValid && g_applied == g_filter) {
        return;
    }
    g_applied = g_filter;
    g_appliedValid = true;
    (void)search(g_snapshot, g_filter, g_rows);
}

/** Points the detail view at one definition and brings that tab forward. */
void open_definition(std::uint16_t definitionIndex) noexcept {
    g_selected = definitionIndex;
    g_hasSelection = true;
    g_openDetail = true;
}

/** Small button that opens another definition. The index in the ID keeps repeated buttons apart. */
[[nodiscard]] bool open_button(std::uint16_t definitionIndex) noexcept {
    const Text label = fmt("Open##%u", static_cast<unsigned>(definitionIndex));
    return ImGui::SmallButton(label.data());
}

/** Name line, or the numeric identity when no name is available. */
void draw_name(const build::items::Definition& item) noexcept {
    const std::string_view name = stubs::item_name_for_hash(item.definitionHash);
    if (name.empty()) {
        ImGui::TextUnformatted("Unnamed definition");
    } else {
        ImGui::TextUnformatted(name.data(), name.data() + name.size());
    }
}

/** Result row for one item. The ordinal keeps the ID unique when two rows share a label. */
void draw_result_row(const build::items::Definition& item, int ordinal) noexcept {
    const auto* detail = detail_at(g_snapshot, item.definitionIndex);
    const std::string_view name = stubs::item_name_for_hash(item.definitionHash);
    std::array<char, 96> nameText{};
    if (name.empty()) {
        (void)std::snprintf(nameText.data(), nameText.size(), "Unnamed definition");
    } else {
        (void)std::snprintf(nameText.data(), nameText.size(), "%.*s",
                            static_cast<int>(name.size()), name.data());
    }
    const auto sockets = detail != nullptr ? static_cast<unsigned>(detail->ordinarySocketCount) : 0U;
    const auto perks = detail != nullptr ? static_cast<unsigned>(detail->sandboxPerkCount) : 0U;
    char label[224];
    (void)std::snprintf(label, sizeof(label),
                        "%s   0x%08X   #%u   %s   bucket %u   sockets %u   perks %u##%d",
                        nameText.data(), static_cast<unsigned>(item.definitionHash),
                        static_cast<unsigned>(item.definitionIndex), tier_label(item.tier),
                        static_cast<unsigned>(item.bucketId), sockets, perks, ordinal);
    if (ImGui::Selectable(label)) {
        open_definition(item.definitionIndex);
    }
}

void draw_browse() noexcept {
    (void)core::ui::components::filter::input("##inspector_search",
                                              "Hash (0x...) or definition index",
                                              g_filter.query.data(), g_filter.query.size());

    int tier = g_filter.tier < 0 ? 0 : g_filter.tier;
    ImGui::SetNextItemWidth(core::ui::scaling::dpi::pixels(kTierWidth));
    if (ImGui::Combo("##inspector_tier", &tier,
                     "All tiers\0Common\0Uncommon\0Rare\0Legendary\0Exotic\0")) {
        g_filter.tier = tier == 0 ? -1 : tier;
    }
    ImGui::SameLine();
    (void)ImGui::Checkbox("Sockets", &g_filter.requireSockets);
    ImGui::SameLine();
    (void)ImGui::Checkbox("Masterwork", &g_filter.requireMasterwork);
    ImGui::SameLine();
    (void)ImGui::Checkbox("Sandbox perks", &g_filter.requireSandboxPerks);
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        reload();
    }

    refresh_rows();
    ImGui::TextDisabled("%zu of %zu definitions", g_rows.size(), g_snapshot.itemRows.size());
    ImGui::Separator();

    const bool open = ImGui::BeginChild("##inspector_results", ImVec2(0.0F, 0.0F));
    if (open) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(g_rows.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& item = g_snapshot.itemRows[g_rows[static_cast<std::size_t>(row)]];
                draw_result_row(item, row);
            }
        }
    }
    ImGui::EndChild();
}

void draw_header(const build::items::Definition& item) noexcept {
    draw_name(item);
    ImGui::TextDisabled("0x%08X   native index %u   %s", static_cast<unsigned>(item.definitionHash),
                        static_cast<unsigned>(item.definitionIndex), tier_label(item.tier));
    ImGui::Spacing();
}

void draw_overview(const Snapshot& snapshot,
                   const build::items::Definition& item,
                   const build::items::details::Definition* detail) noexcept {
    const std::string_view name = stubs::item_name_for_hash(item.definitionHash);
    {
        const Facts facts("##inspector_overview");
        facts.row("Definition hash", fmt("0x%08X", static_cast<unsigned>(item.definitionHash)));
        facts.row("Native index", fmt("%u", static_cast<unsigned>(item.definitionIndex)));
        facts.row("Name", name.empty() ? fmt("Not in Dawn build data") : fmt("%.*s", static_cast<int>(name.size()), name.data()));
        facts.row("Tier", fmt("%s", tier_label(item.tier)));

        build::inventory::buckets::Descriptor descriptor{};
        if (bucket(item.bucketId, descriptor)) {
            facts.row("Inventory bucket",
                      fmt("%u (%s), %u slot(s) from slot %u",
                          static_cast<unsigned>(item.bucketId), array_label(descriptor.arraySelector),
                          static_cast<unsigned>(descriptor.slotCount),
                          static_cast<unsigned>(descriptor.firstSlot)));
        } else {
            facts.row("Inventory bucket", fmt("%u (descriptor not loaded)", static_cast<unsigned>(item.bucketId)));
        }

        facts.row("Masterwork plug",
                  build::items::masterwork_plug(item)
                      ? fmt("Yes, tier %u on stat row %u", static_cast<unsigned>(item.actionStatValue),
                            static_cast<unsigned>(item.actionStatRow))
                      : fmt("No"));

        if (!snapshot.collectiblesReady) {
            facts.row("Collections", fmt("Not loaded"));
        } else {
            const Acquisition acquisition = acquisition_of(snapshot, item.definitionIndex);
            facts.row("Collections",
                      acquisition.granted
                          ? fmt("Granted by collectible %u", static_cast<unsigned>(acquisition.collectibleIndex))
                          : fmt("Not granted by a Collections entry"));
        }

        if (detail != nullptr) {
            facts.row("Maximum stack", fmt("%d", static_cast<int>(detail->maxStackSize)));
            facts.row("Quantity rule",
                      fmt("%s", detail->instancedDefinitionState == build::items::details::InstancedDefinitionState::instanced
                                    ? "One instance per copy" : "Stackable"));
            facts.row("Equipment slot",
                      detail->equipmentSlot.has_value()
                          ? fmt("Slot %d", static_cast<int>(*detail->equipmentSlot))
                          : fmt("None"));
            facts.row("Ordinary sockets", fmt("%u lane(s)", static_cast<unsigned>(detail->ordinarySocketCount)));
            facts.row("Sandbox perks", fmt("%u", static_cast<unsigned>(detail->sandboxPerkCount)));
            facts.row("Stat rows", fmt("%u", static_cast<unsigned>(detail->statCount)));
            facts.row("Gear art", index_text(detail->gearArtIndex, build::items::details::kUnavailableArtIndex));
        } else {
            facts.row("Configured detail", fmt("Not published for this definition"));
        }
    }
    ImGui::Spacing();
    if (detail == nullptr) {
        ImGui::TextDisabled("Stats, sockets, and perks come from the configured detail, which this definition does not have.");
    }
}

void draw_stat_label(std::array<char, 96>& out, std::uint8_t row,
                     const build::constants::InvestmentConstants& constants, bool constantsReady) noexcept {
    const std::string_view named = stubs::stat_name(row);
    if (!named.empty()) {
        (void)std::snprintf(out.data(), out.size(), "%.*s", static_cast<int>(named.size()), named.data());
        return;
    }
    if (constantsReady && constants.extracted) {
        if (row == constants.lightStatRow) {
            (void)std::snprintf(out.data(), out.size(), "Light (banner power)");
            return;
        }
        if (row == constants.weaponPowerStatRow) {
            (void)std::snprintf(out.data(), out.size(), "Weapon power");
            return;
        }
        for (std::size_t slot = 0; slot < constants.characterStatRows.size(); ++slot) {
            if (row == constants.characterStatRows[slot]) {
                (void)std::snprintf(out.data(), out.size(), "Character stat %zu", slot + 1);
                return;
            }
        }
    }
    (void)std::snprintf(out.data(), out.size(), "Stat row %u", static_cast<unsigned>(row));
}

void draw_stats(const build::items::Definition& item,
                const build::items::details::Definition* detail) noexcept {
    if (build::items::masterwork_plug(item)) {
        const Facts facts("##inspector_masterwork");
        facts.row("Masterwork plug",
                  fmt("Tier %u on stat row %u", static_cast<unsigned>(item.actionStatValue),
                      static_cast<unsigned>(item.actionStatRow)));
        ImGui::Spacing();
    }
    if (detail == nullptr) {
        ImGui::TextDisabled("No configured detail, so no stat contributions are published.");
        return;
    }
    const std::size_t count = clamp_count(detail->statCount, detail->stats.size());
    if (count == 0) {
        ImGui::TextDisabled("This definition contributes no stats.");
        return;
    }

    build::constants::InvestmentConstants constants{};
    const bool constantsReady = investment(constants);
    if (!ImGui::BeginTable("##inspector_stat_rows", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        return;
    }
    ImGui::TableSetupColumn("Stat");
    ImGui::TableSetupColumn("Investment value");
    ImGui::TableSetupColumn("Row");
    ImGui::TableHeadersRow();
    for (std::size_t slot = 0; slot < count; ++slot) {
        const auto& stat = detail->stats[slot];
        std::array<char, 96> label{};
        draw_stat_label(label, stat.row, constants, constantsReady);
        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(slot));
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label.data());
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d", static_cast<int>(stat.value));
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%u", static_cast<unsigned>(stat.row));
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void draw_sockets(const Snapshot& snapshot,
                  const build::items::Definition& item,
                  const build::items::details::Definition* detail) noexcept {
    if (detail == nullptr) {
        ImGui::TextDisabled("No configured detail, so no sockets are published.");
        return;
    }
    if (!snapshot.socketsReady) {
        ImGui::TextDisabled("Socket plug rules are not loaded. Pools below show as unavailable.");
    }
    {
        const Facts facts("##inspector_socket_facts");
        facts.row("Ordinary sockets",
                  fmt("%s", detail->ordinarySocketState == build::items::details::OrdinarySocketState::present
                                ? "Declared" : "Not declared"));
        facts.row("Socket lanes", fmt("%u", static_cast<unsigned>(detail->ordinarySocketCount)));
        facts.row("Socket entry list", fmt("Row %u", static_cast<unsigned>(detail->socketEntryListIndex)));
        facts.row("Insertion cost", set_text(item.insertionMaterialRequirementSetIndex));
        facts.row("Enable cost", set_text(item.enabledMaterialRequirementSetIndex));
    }
    ImGui::Spacing();

    const std::size_t lanes = clamp_count(detail->ordinarySocketCount, detail->socketTypes.size());
    if (lanes == 0) {
        ImGui::TextDisabled("No ordinary sockets.");
        return;
    }

    std::vector<PlugPool> choices(lanes);
    std::vector<PlugPool> rolls(lanes);
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        const auto laneIndex = static_cast<std::uint8_t>(lane);
        choices[lane] = plug_pool(item.definitionIndex, laneIndex, false);
        rolls[lane] = plug_pool(item.definitionIndex, laneIndex, true);
    }

    if (ImGui::BeginTable("##inspector_socket_lanes", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Lane");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Initial plug");
        ImGui::TableSetupColumn("Choices");
        ImGui::TableSetupColumn("Roll pool");
        ImGui::TableHeadersRow();
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(lane));
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%zu", lane + 1);
            ImGui::TableSetColumnIndex(1);
            const std::uint16_t socketType = detail->socketTypes[lane];
            if (socketType == build::items::details::kUnavailableSocketType) {
                ImGui::TextDisabled("Undeclared");
            } else {
                ImGui::Text("%u", static_cast<unsigned>(socketType));
            }
            ImGui::TableSetColumnIndex(2);
            const std::uint16_t initial = detail->initialPlugIndices[lane];
            if (initial == build::items::details::kUnavailableItemIndex) {
                ImGui::TextDisabled("Empty");
            } else {
                ImGui::Text("#%u", static_cast<unsigned>(initial));
                ImGui::SameLine();
                if (open_button(initial)) {
                    open_definition(initial);
                }
            }
            ImGui::TableSetColumnIndex(3);
            if (choices[lane].complete) {
                ImGui::Text("%zu", choices[lane].total);
            } else {
                ImGui::TextDisabled("Unavailable");
            }
            ImGui::TableSetColumnIndex(4);
            if (rolls[lane].complete) {
                ImGui::Text("%zu", rolls[lane].total);
            } else {
                ImGui::TextDisabled("Unavailable");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    for (std::size_t lane = 0; lane < lanes; ++lane) {
        const PlugPool& pool = choices[lane];
        if (!pool.complete || pool.members.empty()) {
            continue;
        }
        ImGui::PushID(static_cast<int>(lane));
        if (ImGui::CollapsingHeader("Plug choices", ImGuiTreeNodeFlags_None)) {
            for (std::size_t member = 0; member < pool.members.size(); ++member) {
                ImGui::PushID(static_cast<int>(member));
                if (member % 6 != 0) {
                    ImGui::SameLine();
                }
                if (open_button(pool.members[member])) {
                    open_definition(pool.members[member]);
                }
                ImGui::PopID();
            }
            if (pool.total > pool.members.size()) {
                ImGui::TextDisabled("Showing %zu of %zu", pool.members.size(), pool.total);
            }
        }
        ImGui::PopID();
    }
}

void draw_perks(const Snapshot& snapshot,
                const build::items::Definition& item,
                const build::items::details::Definition* detail) noexcept {
    if (detail == nullptr) {
        ImGui::TextDisabled("No configured detail, so no sandbox perks are published.");
        return;
    }
    const std::size_t count = clamp_count(detail->sandboxPerkCount, detail->sandboxPerks.size());
    if (count == 0) {
        ImGui::TextDisabled("This definition declares no sandbox perks.");
        return;
    }

    for (std::size_t slot = 0; slot < count; ++slot) {
        const std::uint16_t perk = detail->sandboxPerks[slot];
        ImGui::PushID(static_cast<int>(slot));
        ImGui::Text("Perk %u", static_cast<unsigned>(perk));
        const std::string_view name = stubs::perk_name(perk);
        if (name.empty()) {
            ImGui::TextDisabled("Name not in Dawn build data");
        } else {
            ImGui::TextUnformatted(name.data(), name.data() + name.size());
        }

        std::size_t others = 0;
        const auto* carriers = carriers_of(snapshot, perk);
        if (carriers != nullptr) {
            for (const std::uint16_t carrier : *carriers) {
                if (carrier != item.definitionIndex) {
                    ++others;
                }
            }
        }
        ImGui::TextDisabled("Carried by %zu other item(s)", others);
        if (others > 0 && ImGui::CollapsingHeader("Carriers")) {
            std::size_t shown = 0;
            for (const std::uint16_t carrier : *carriers) {
                if (carrier == item.definitionIndex) {
                    continue;
                }
                if (shown >= kListLimit) {
                    ImGui::TextDisabled("Showing %zu of %zu", shown, others);
                    break;
                }
                const auto* carrierItem = item_at(snapshot, carrier);
                if (carrierItem == nullptr) {
                    continue;
                }
                ImGui::PushID(static_cast<int>(carrier));
                if (open_button(carrier)) {
                    open_definition(carrier);
                }
                ImGui::SameLine();
                ImGui::Text("0x%08X   #%u", static_cast<unsigned>(carrierItem->definitionHash),
                            static_cast<unsigned>(carrier));
                ImGui::PopID();
                ++shown;
            }
        }
        ImGui::Spacing();
        ImGui::PopID();
    }
    ImGui::TextDisabled("Declaration-only status and runtime behavior are not in Dawn build data.");
}

void draw_requirement_rows(const char* id,
                           const build::material_requirements::Requirement* rows,
                           std::size_t count) noexcept {
    if (count == 0) {
        ImGui::TextDisabled("No material rows.");
        return;
    }
    if (!ImGui::BeginTable(id, 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        return;
    }
    ImGui::TableSetupColumn("Quantity");
    ImGui::TableSetupColumn("Item");
    ImGui::TableSetupColumn("Condition");
    ImGui::TableSetupColumn("Flags");
    ImGui::TableHeadersRow();
    for (std::size_t row = 0; row < count; ++row) {
        const auto& requirement = rows[row];
        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(row));
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%u", static_cast<unsigned>(requirement.quantity));
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("#%u", static_cast<unsigned>(requirement.itemDefinitionIndex));
        ImGui::SameLine();
        if (open_button(requirement.itemDefinitionIndex)) {
            open_definition(requirement.itemDefinitionIndex);
        }
        ImGui::TableSetColumnIndex(2);
        if (requirement.condition == build::material_requirements::kUnconditionalRequirement) {
            ImGui::TextDisabled("Always");
        } else {
            ImGui::Text("Variant %u", static_cast<unsigned>(requirement.condition));
        }
        ImGui::TableSetColumnIndex(3);
        ImGui::TextDisabled("%s%s", requirement.deleteOnAction ? "Consumed " : "",
                            requirement.omitFromRequirements ? "Hidden" : "");
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void draw_collection_rows(const build::collectibles::MaterialRequirement* rows, std::size_t count) noexcept {
    if (count == 0) {
        ImGui::TextDisabled("No material rows.");
        return;
    }
    if (!ImGui::BeginTable("##inspector_acquisition_rows", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        return;
    }
    ImGui::TableSetupColumn("Quantity");
    ImGui::TableSetupColumn("Item");
    ImGui::TableSetupColumn("Consumed");
    ImGui::TableSetupColumn("Hidden");
    ImGui::TableHeadersRow();
    for (std::size_t row = 0; row < count; ++row) {
        const auto& requirement = rows[row];
        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(row));
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%u", static_cast<unsigned>(requirement.quantity));
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("#%u", static_cast<unsigned>(requirement.itemDefinitionIndex));
        ImGui::SameLine();
        if (open_button(requirement.itemDefinitionIndex)) {
            open_definition(requirement.itemDefinitionIndex);
        }
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(requirement.deleteOnAction ? "Yes" : "No");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(requirement.omitFromRequirements ? "Yes" : "No");
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void draw_progression(const Snapshot& snapshot, const build::items::Definition& item) noexcept {
    ImGui::TextUnformatted("Collections acquisition");
    if (!snapshot.collectiblesReady) {
        ImGui::TextDisabled("Collections data is not loaded.");
    } else {
        const Acquisition acquisition = acquisition_of(snapshot, item.definitionIndex);
        if (!acquisition.granted) {
            ImGui::TextDisabled("No Collections entry grants this definition.");
        } else {
            {
                const Facts facts("##inspector_acquisition");
                facts.row("Collectible index", fmt("%u", static_cast<unsigned>(acquisition.collectibleIndex)));
                facts.row("Collectible hash",
                          fmt("0x%08X", static_cast<unsigned>(acquisition.collectible.collectibleHash)));
                facts.row("Material set", set_text(acquisition.collectible.materialRequirementSetIndex));
            }
            const std::size_t count = clamp_count(acquisition.collectible.materialRequirementCount,
                                                  acquisition.collectible.materialRequirements.size());
            draw_collection_rows(acquisition.collectible.materialRequirements.data(), count);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Insertion and enable costs");
    if (!snapshot.materialsReady) {
        ImGui::TextDisabled("Material requirement sets are not loaded.");
    }
    const struct {
        const char* label;
        std::uint16_t setIndex;
    } costs[] = {
        {"Insertion", item.insertionMaterialRequirementSetIndex},
        {"Enable", item.enabledMaterialRequirementSetIndex},
    };
    for (const auto& cost : costs) {
        ImGui::PushID(cost.label);
        ImGui::TextDisabled("%s: %s", cost.label, set_text(cost.setIndex).data());
        if (cost.setIndex != build::items::kUnavailableMaterialRequirementSetIndex && snapshot.materialsReady) {
            build::material_requirements::Definition set{};
            if (material_set(cost.setIndex, set)) {
                const std::size_t count = clamp_count(set.requirementCount, set.requirements.size());
                draw_requirement_rows("##inspector_cost_rows", set.requirements.data(), count);
            } else {
                ImGui::TextDisabled("Set is not published.");
            }
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Objective, unlock, and progression-slot relations are not ported. See NOTES.md.");
}

void draw_technical(const build::items::Definition& item,
                    const build::items::details::Definition* detail) noexcept {
    {
        const Facts facts("##inspector_technical");
        facts.row("Plug category hash",
                  item.plugCategoryHash == 0 ? fmt("None") : fmt("0x%08X", static_cast<unsigned>(item.plugCategoryHash)));
        facts.row("Insertion set index", index_text(item.insertionMaterialRequirementSetIndex,
                                                    build::items::kUnavailableMaterialRequirementSetIndex));
        facts.row("Enable set index", index_text(item.enabledMaterialRequirementSetIndex,
                                                 build::items::kUnavailableMaterialRequirementSetIndex));
        facts.row("Action stat row", fmt("%u (value %u)", static_cast<unsigned>(item.actionStatRow),
                                         static_cast<unsigned>(item.actionStatValue)));
        if (detail != nullptr) {
            facts.row("Stored instance flag",
                      fmt("%s", detail->instancedDefinitionState == build::items::details::InstancedDefinitionState::instanced
                                    ? "Instanced (1)" : "Stackable (0)"));
            facts.row("Socket entry list index", fmt("%u", static_cast<unsigned>(detail->socketEntryListIndex)));
            facts.row("Gear art index", index_text(detail->gearArtIndex, build::items::details::kUnavailableArtIndex));
            facts.row("Art arrangements",
                      fmt("%s, %s, %s, %s",
                          index_text(detail->artArrangementIndices[0], build::items::details::kUnavailableArtIndex).data(),
                          index_text(detail->artArrangementIndices[1], build::items::details::kUnavailableArtIndex).data(),
                          index_text(detail->artArrangementIndices[2], build::items::details::kUnavailableArtIndex).data(),
                          index_text(detail->artArrangementIndices[3], build::items::details::kUnavailableArtIndex).data()));
            facts.row("Render overrides", fmt("%u", static_cast<unsigned>(detail->renderOverrideCount)));
        }
    }
    if (detail == nullptr || detail->renderOverrideCount == 0) {
        return;
    }
    ImGui::Spacing();
    const std::size_t count = clamp_count(detail->renderOverrideCount, detail->renderOverrides.size());
    if (ImGui::BeginTable("##inspector_render_overrides", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Stage");
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();
        for (std::size_t row = 0; row < count; ++row) {
            const auto& entry = detail->renderOverrides[row];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(row));
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%u", static_cast<unsigned>(entry.stage));
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", static_cast<int>(entry.key));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%u", static_cast<unsigned>(entry.value));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void draw_detail(const Snapshot& snapshot) noexcept {
    const build::items::Definition* item = g_hasSelection ? item_at(snapshot, g_selected) : nullptr;
    if (item == nullptr) {
        ImGui::TextWrapped("Choose a definition from Browse, or open one from a carrier or plug list.");
        return;
    }
    const auto* detail = detail_at(snapshot, item->definitionIndex);
    draw_header(*item);

    if (!ImGui::BeginTabBar("##inspector_detail_pages")) {
        return;
    }
    if (ImGui::BeginTabItem("Overview")) {
        draw_overview(snapshot, *item, detail);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Stats")) {
        draw_stats(*item, detail);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Sockets")) {
        draw_sockets(snapshot, *item, detail);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Perks")) {
        draw_perks(snapshot, *item, detail);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Progression")) {
        draw_progression(snapshot, *item);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Technical")) {
        draw_technical(*item, detail);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

} // namespace

/** Draws the item and perk definition inspector inside the active Core UI frame. */
void draw() noexcept {
    ensure_loaded();
    core::ui::components::section::header(
        "Item and Perk Definitions",
        "Read-only view of installed item definitions. Nothing here changes State.");
    if (!g_loaded) {
        ImGui::TextWrapped("Item definitions are not loaded yet. Retry once the client has finished reading build data.");
        if (ImGui::Button("Retry")) {
            reload();
        }
        return;
    }
    if (!g_snapshot.detailsReady) {
        ImGui::TextWrapped("Configured item details are not ready, so stats, sockets, and perks show as unavailable.");
    }

    if (!ImGui::BeginTabBar("##inspector_tabs")) {
        return;
    }
    if (ImGui::BeginTabItem("Browse")) {
        draw_browse();
        ImGui::EndTabItem();
    }
    const ImGuiTabItemFlags flags = g_openDetail ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    g_openDetail = false;
    if (ImGui::BeginTabItem("Detail", nullptr, flags)) {
        draw_detail(g_snapshot);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

} // namespace dawn::client::ui::inspector
