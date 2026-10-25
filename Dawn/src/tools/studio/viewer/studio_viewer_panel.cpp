/** The studio viewer's panel. Every tab is a list on the left and the selected item's details on the right. */

#include "studio_viewer_panel.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "../../../state/build_data/items/details/definition.h"
#include "studio_viewer_classify.h"
#include "studio_viewer_load.h"
#include "studio_viewer_model.h"
#include "studio_viewer_stubs.h"

namespace dawn::tools::studio::viewer {
namespace {

namespace editor = ::dawn::state::editor;
namespace details = ::dawn::state::build_data::items::details;
namespace vendors = ::dawn::state::build_data::vendors;

/** Left list width on the two-pane tabs, in pixels. */
constexpr float kListWidth = 320.0F;
/** Column where detail values start, in pixels. */
constexpr float kDetailValueColumn = 170.0F;
/** Width of the folder inputs, in pixels. */
constexpr float kPathWidth = 480.0F;
/** Width of the filter input, in pixels. */
constexpr float kFilterWidth = 320.0F;
/** Height of the mission script preview, in pixels. */
constexpr float kPreviewHeight = 240.0F;

/** Everything the panel keeps between frames. It is read-only apart from the folder inputs and filter. */
struct PanelState {
    std::array<char, 512> packageDirectory{};
    std::array<char, 512> projectDirectory{};
    std::array<char, 96> filter{};
    LoadJob job;
    std::optional<Snapshot> snapshot;
    std::string status;
    std::array<std::size_t, kTabCount> selected{kNoSelection, kNoSelection, kNoSelection, kNoSelection,
                                                kNoSelection};
    std::size_t previewRow{kNoSelection};
    std::string preview;
};

/** @return The one panel state. Function-local so its construction happens on first use. */
PanelState& state() noexcept {
    static PanelState instance;
    return instance;
}

/** @return The selection slot of one tab. */
constexpr std::size_t slot(Tab tab) noexcept {
    return static_cast<std::size_t>(tab);
}

std::string yes_no(bool value) {
    return value ? "Yes" : "No";
}

/** One label and its value on one line, with the value wrapped at the window edge. */
void detail_row(const char* label, const std::string& value) {
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(kDetailValueColumn);
    ImGui::TextWrapped("%s", value.c_str());
}

/** @return True when needle appears in haystack, ignoring ASCII case. An empty needle matches. */
bool contains_ci(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    const auto same = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), same) != haystack.end();
}

/** @return True when the filter matches any of the row's searchable fields. */
bool matches(std::string_view filter, std::initializer_list<std::string_view> fields) {
    return std::any_of(fields.begin(), fields.end(),
                       [filter](std::string_view field) { return contains_ci(field, filter); });
}

std::string_view filter_text(const PanelState& s) noexcept {
    return std::string_view(s.filter.data());
}

/** One selectable list row. Returns true when it was clicked this frame. */
bool list_entry(std::size_t index, const std::string& label, bool selected) {
    ImGui::PushID(static_cast<int>(index));
    const bool clicked = ImGui::Selectable(label.c_str(), selected);
    ImGui::PopID();
    return clicked;
}

std::string_view gear_kind_name(editor::GearKind kind) noexcept {
    switch (kind) {
    case editor::GearKind::weapon: return "Weapon";
    case editor::GearKind::armor: return "Armor";
    case editor::GearKind::cosmetic: return "Cosmetic";
    case editor::GearKind::subclass: return "Subclass";
    case editor::GearKind::other: break;
    }
    return "Other";
}

std::string_view damage_name(editor::DamageType type) noexcept {
    switch (type) {
    case editor::DamageType::arc: return "Arc";
    case editor::DamageType::solar: return "Solar";
    case editor::DamageType::void_: return "Void";
    case editor::DamageType::none: break;
    }
    return "None";
}

std::string_view ammo_name(editor::Ammo ammo) noexcept {
    switch (ammo) {
    case editor::Ammo::primary: return "Primary";
    case editor::Ammo::special: return "Special";
    case editor::Ammo::heavy: return "Heavy";
    case editor::Ammo::none: break;
    }
    return "None";
}

/** @return The name of an item by its definition index, or the unavailable marker's name. */
std::string item_name(const editor::Catalog& catalog, std::uint16_t index) {
    if (index == details::kUnavailableItemIndex) {
        return "None";
    }
    if (const editor::CatalogItem* found = catalog.index(index); found != nullptr && !found->name.empty()) {
        return found->name;
    }
    return "Item " + std::to_string(index);
}

std::string stat_name(const editor::Catalog& catalog, std::uint16_t row) {
    const auto found = catalog.statNames.find(row);
    return found != catalog.statNames.end() ? found->second : "Stat " + std::to_string(row);
}

void draw_asset_detail(const AssetRow& row) {
    ImGui::TextWrapped("%s", row.name.c_str());
    ImGui::Separator();
    detail_row("Tag", hex32(row.tag));
    detail_row("Class", stubs::describe_class(row.classId));
    detail_row("Category", row.category);
    detail_row("Patch", std::to_string(row.patchIndex));
    detail_row("Bytes", std::string(stubs::asset_bytes_message()));
}

void draw_assets(PanelState& s, const Snapshot& snapshot) {
    std::size_t& selected = s.selected[slot(Tab::assets)];
    const std::string_view filter = filter_text(s);
    ImGui::BeginChild("##studio_assets_list", ImVec2(kListWidth, 0.0F), true);
    if (snapshot.assets.empty()) {
        ImGui::TextDisabled("No named entries were found.");
    }
    for (std::size_t i = 0; i < snapshot.assets.size(); ++i) {
        const AssetRow& row = snapshot.assets[i];
        if (matches(filter, {row.name, row.category}) && list_entry(i, row.name, selected == i)) {
            selected = i;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##studio_assets_detail", ImVec2(0.0F, 0.0F), true);
    if (selected < snapshot.assets.size()) {
        draw_asset_detail(snapshot.assets[selected]);
    } else {
        ImGui::TextDisabled("Select an asset to see its details.");
    }
    ImGui::EndChild();
}

void draw_map_detail(const MapRow& row) {
    ImGui::TextWrapped("%s", row.name.c_str());
    ImGui::Separator();
    detail_row("Tag", hex32(row.tag));
    detail_row("Bubbles", std::string(stubs::map_bubbles_message()));
    detail_row("Activities", "Not ported: scenario links. The activity table is not read.");
}

void draw_maps(PanelState& s, const Snapshot& snapshot) {
    std::size_t& selected = s.selected[slot(Tab::maps)];
    const std::string_view filter = filter_text(s);
    ImGui::BeginChild("##studio_maps_list", ImVec2(kListWidth, 0.0F), true);
    if (snapshot.maps.empty()) {
        ImGui::TextDisabled("No map roots were found.");
    }
    for (std::size_t i = 0; i < snapshot.maps.size(); ++i) {
        const MapRow& row = snapshot.maps[i];
        if (matches(filter, {row.name}) && list_entry(i, row.name, selected == i)) {
            selected = i;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##studio_maps_detail", ImVec2(0.0F, 0.0F), true);
    if (selected < snapshot.maps.size()) {
        draw_map_detail(snapshot.maps[selected]);
    } else {
        ImGui::TextDisabled("Select a map to see its details.");
    }
    ImGui::EndChild();
}

void draw_mission_detail(PanelState& s, const MissionRow& row) {
    ImGui::TextWrapped("%s", row.name.c_str());
    ImGui::Separator();
    detail_row("Relative path", row.relativePath);
    detail_row("Full path", row.fullPath);
    detail_row("Bytes", std::to_string(row.bytes));
    detail_row("Validation", std::string(stubs::mission_validation_message()));
    ImGui::Spacing();
    ImGui::TextDisabled("Preview");
    ImGui::BeginChild("##studio_mission_preview", ImVec2(0.0F, kPreviewHeight), true);
    ImGui::TextUnformatted(s.preview.c_str());
    ImGui::EndChild();
}

void draw_missions(PanelState& s, const Snapshot& snapshot) {
    std::size_t& selected = s.selected[slot(Tab::missions)];
    const std::string_view filter = filter_text(s);
    ImGui::BeginChild("##studio_missions_list", ImVec2(kListWidth, 0.0F), true);
    if (snapshot.missions.empty()) {
        ImGui::TextDisabled("No mission scripts were found. Set a project folder with scripts/.");
    }
    for (std::size_t i = 0; i < snapshot.missions.size(); ++i) {
        const MissionRow& row = snapshot.missions[i];
        if (matches(filter, {row.name, row.relativePath}) && list_entry(i, row.name, selected == i)) {
            selected = i;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##studio_missions_detail", ImVec2(0.0F, 0.0F), true);
    if (selected < snapshot.missions.size()) {
        if (s.previewRow != selected) {
            s.previewRow = selected;
            s.preview.clear();
            if (!read_preview(snapshot.missions[selected].fullPath, s.preview)) {
                s.preview = "Could not read this script.";
            }
        }
        draw_mission_detail(s, snapshot.missions[selected]);
    } else {
        ImGui::TextDisabled("Select a mission script to see its details.");
    }
    ImGui::EndChild();
}

/** Item details. Every value comes from the Dawn catalog; objectives are reported as a stub. */
void draw_item_detail(const Snapshot& snapshot, const ItemRow& row) {
    if (row.catalogIndex >= snapshot.catalog.items.size()) {
        return;
    }
    const editor::CatalogItem& item = snapshot.catalog.items[row.catalogIndex];
    const details::Definition& detail = item.detail;

    ImGui::TextWrapped("%s", row.label.c_str());
    ImGui::Separator();
    detail_row("Type", row.type);
    detail_row("Category", row.category);
    detail_row("Description", item.description);
    detail_row("Hash", hex32(item.definition.definitionHash));
    detail_row("Definition index", std::to_string(item.definition.definitionIndex));
    detail_row("Gear kind", std::string(gear_kind_name(item.kind)));

    std::string bucket = std::to_string(item.definition.bucketId);
    if (const std::string_view slotName = classify::slot_name(item.definition.bucketId); !slotName.empty()) {
        bucket += " (";
        bucket += slotName;
        bucket += ")";
    }
    detail_row("Bucket", bucket);
    detail_row("Tier", classify::tier_name(item.definition.tier));
    detail_row("Maximum stack", std::to_string(detail.maxStackSize));
    detail_row("Instanced",
               yes_no(detail.instancedDefinitionState == details::InstancedDefinitionState::instanced));
    detail_row("Plug", yes_no(item.plug));
    detail_row("Damage type", std::string(damage_name(item.damageType)));
    detail_row("Ammo", std::string(ammo_name(item.ammo)));
    detail_row("Primary stat", item.primaryStatRow == editor::kNoStatRow
                                   ? std::string("None")
                                   : stat_name(snapshot.catalog, item.primaryStatRow));
    detail_row("Unnamed", yes_no(item.unnamed));
    detail_row("Inert plug", yes_no(item.inert));

    if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::size_t count = std::min<std::size_t>(detail.statCount, detail.stats.size());
        bool any = false;
        for (std::size_t i = 0; i < count; ++i) {
            const details::Stat& stat = detail.stats[i];
            if (stat.row == details::kEmptyStatRow) {
                continue;
            }
            any = true;
            const std::uint16_t statRow = stat.row;
            const std::int32_t shown =
                editor::display_stat(snapshot.catalog, item.statGroupIndex, statRow, stat.value);
            ImGui::TextWrapped("%s: %d (stored %d)", stat_name(snapshot.catalog, statRow).c_str(), shown,
                               stat.value);
        }
        if (!any) {
            ImGui::TextDisabled("This item declares no stats.");
        }
    }

    if (ImGui::CollapsingHeader("Sockets", ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::size_t lanes = std::min(detail.initialPlugIndices.size(), detail.socketTypes.size());
        bool any = false;
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            if (detail.socketTypes[lane] == details::kUnavailableSocketType) {
                continue;
            }
            any = true;
            const std::size_t compatible = lane < item.compatible.size() ? item.compatible[lane].size() : 0U;
            ImGui::TextWrapped("Socket %zu: type %u, initial plug %s, %zu compatible plugs", lane,
                               static_cast<unsigned>(detail.socketTypes[lane]),
                               item_name(snapshot.catalog, detail.initialPlugIndices[lane]).c_str(),
                               compatible);
        }
        if (!any) {
            ImGui::TextDisabled("This item declares no sockets.");
        }
    }

    if (ImGui::CollapsingHeader("Sandbox perks")) {
        const std::size_t count = std::min<std::size_t>(detail.sandboxPerkCount, detail.sandboxPerks.size());
        for (std::size_t i = 0; i < count; ++i) {
            ImGui::TextWrapped("Sandbox perk %u", static_cast<unsigned>(detail.sandboxPerks[i]));
        }
        if (count == 0) {
            ImGui::TextDisabled("This item declares no sandbox perks.");
        }
        ImGui::TextDisabled("Perks are shown by index. Their names are not ported.");
    }

    ImGui::Spacing();
    ImGui::TextDisabled("%s", std::string(stubs::item_has_objectives(item)
                                              ? "Objective blocks are present."
                                              : "Objective blocks are not exposed, so Quests come from the type line.")
                                  .c_str());
}

void draw_items(PanelState& s, const Snapshot& snapshot) {
    std::size_t& selected = s.selected[slot(Tab::items)];
    const std::string_view filter = filter_text(s);
    ImGui::BeginChild("##studio_items_list", ImVec2(kListWidth, 0.0F), true);
    if (snapshot.items.empty()) {
        ImGui::TextDisabled("No items are loaded. The item catalog needs the game to be ready.");
    }
    for (std::size_t i = 0; i < snapshot.items.size(); ++i) {
        const ItemRow& row = snapshot.items[i];
        if (matches(filter, {row.label, row.type, row.category}) && list_entry(i, row.label, selected == i)) {
            selected = i;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##studio_items_detail", ImVec2(0.0F, 0.0F), true);
    if (selected < snapshot.items.size()) {
        draw_item_detail(snapshot, snapshot.items[selected]);
    } else {
        ImGui::TextDisabled("Select an item to see its details.");
    }
    ImGui::EndChild();
}

void draw_vendor_detail(const Snapshot& snapshot, const VendorRow& row) {
    ImGui::TextWrapped("%s", row.name.c_str());
    ImGui::Separator();
    detail_row("Index", std::to_string(row.index));
    detail_row("Hash", hex32(row.definitionHash));
    detail_row("Definition tag", hex32(row.definitionTag));
    detail_row("Reset interval (raw)", std::to_string(row.resetIntervalRaw));
    detail_row("Reset phase (raw)", std::to_string(row.resetPhaseRaw));
    detail_row("Sale rows", std::to_string(row.saleCount));

    if (ImGui::CollapsingHeader("Sales", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("%s", std::string(stubs::vendor_sale_costs_message()).c_str());
        for (const vendors::SaleRow& sale : snapshot.sales) {
            if (sale.vendorIndex != row.index) {
                continue;
            }
            const std::string secondary = sale.secondaryItemIndex == vendors::kAbsentSecondaryItem
                                              ? std::string("None")
                                              : item_name(snapshot.catalog, sale.secondaryItemIndex);
            const std::string category = sale.categoryIndex == vendors::kAbsentCategoryIndex
                                             ? std::string("None")
                                             : std::to_string(sale.categoryIndex);
            ImGui::TextWrapped("Row %u: %s; secondary %s; category %s", static_cast<unsigned>(sale.rowIndex),
                               item_name(snapshot.catalog, sale.itemIndex).c_str(), secondary.c_str(),
                               category.c_str());
        }
    }
}

void draw_vendors(PanelState& s, const Snapshot& snapshot) {
    std::size_t& selected = s.selected[slot(Tab::vendors)];
    const std::string_view filter = filter_text(s);
    ImGui::BeginChild("##studio_vendors_list", ImVec2(kListWidth, 0.0F), true);
    if (snapshot.vendors.empty()) {
        ImGui::TextDisabled("No vendor rows are loaded. The vendor index needs the game to be ready.");
    }
    for (std::size_t i = 0; i < snapshot.vendors.size(); ++i) {
        const VendorRow& row = snapshot.vendors[i];
        if (matches(filter, {row.name}) && list_entry(i, row.name, selected == i)) {
            selected = i;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##studio_vendors_detail", ImVec2(0.0F, 0.0F), true);
    if (selected < snapshot.vendors.size()) {
        draw_vendor_detail(snapshot, snapshot.vendors[selected]);
    } else {
        ImGui::TextDisabled("Select a vendor to see its details.");
    }
    ImGui::EndChild();
}

void draw_diagnostics(const Snapshot& snapshot) {
    if (snapshot.diagnostics.empty()) {
        return;
    }
    const std::string header = "Diagnostics (" + std::to_string(snapshot.diagnostics.size()) + ")";
    if (ImGui::CollapsingHeader(header.c_str())) {
        for (const std::string& line : snapshot.diagnostics) {
            ImGui::TextWrapped("%s", line.c_str());
        }
    }
}

} // namespace

/** Draws the studio viewer inside the active tools frame. Read-only. */
void draw() noexcept {
    PanelState& s = state();
    LoadOutcome outcome;
    if (s.job.poll(outcome)) {
        if (outcome.ok) {
            s.snapshot = std::move(outcome.snapshot);
            s.selected.fill(kNoSelection);
            s.previewRow = kNoSelection;
            s.preview.clear();
            s.status = "Loaded " + std::to_string(s.snapshot->assets.size()) + " assets and " +
                       std::to_string(s.snapshot->items.size()) + " items.";
        } else {
            s.status = outcome.error;
        }
    }

    ImGui::TextUnformatted("Studio Viewer");
    ImGui::Separator();
    ImGui::TextWrapped("Read-only. Lists assets, maps, mission scripts and investment data, and shows the "
                       "details of the selected item.");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(kPathWidth);
    (void)ImGui::InputText("Package folder##studio_viewer_packages", s.packageDirectory.data(),
                           s.packageDirectory.size());
    ImGui::SetNextItemWidth(kPathWidth);
    (void)ImGui::InputText("Project folder##studio_viewer_project", s.projectDirectory.data(),
                           s.projectDirectory.size());

    const bool busy = s.job.busy();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Load")) {
        LoadRequest request;
        request.packageDirectory = to_wide(s.packageDirectory.data());
        request.projectDirectory = to_wide(s.projectDirectory.data());
        s.status = s.job.start(std::move(request)) ? "Loading..." : "Could not start the load.";
    }
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            s.job.cancel();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Progress %u", s.job.progress());
    }
    if (!s.status.empty()) {
        ImGui::TextDisabled("%s", s.status.c_str());
    }
    ImGui::Spacing();

    if (!s.snapshot.has_value()) {
        ImGui::TextDisabled("Load a package folder to browse assets, maps, missions and investment data.");
        return;
    }
    const Snapshot& snapshot = *s.snapshot;
    ImGui::SetNextItemWidth(kFilterWidth);
    (void)ImGui::InputText("Filter##studio_viewer_filter", s.filter.data(), s.filter.size());
    draw_diagnostics(snapshot);

    if (ImGui::BeginTabBar("##studio_viewer_tabs")) {
        if (ImGui::BeginTabItem("Assets")) {
            draw_assets(s, snapshot);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Maps")) {
            draw_maps(s, snapshot);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Missions")) {
            draw_missions(s, snapshot);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Items")) {
            draw_items(s, snapshot);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Vendors")) {
            draw_vendors(s, snapshot);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

/** Cancels a running load and drops the loaded snapshot. */
void shutdown() noexcept {
    PanelState& s = state();
    s.job.cancel();
    s.snapshot.reset();
    s.selected.fill(kNoSelection);
    s.previewRow = kNoSelection;
    s.preview.clear();
    s.status.clear();
}

} // namespace dawn::tools::studio::viewer
