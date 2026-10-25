/**
 * The Parhelion weapon module. One recipe is edited at a time: its identity, its stock base weapon,
 * its stats, its perks, and its ammo and damage. Check, Build & Stage and Save act on that recipe.
 *
 * NOT COMPILED OR TESTED. The layout follows Dawn's player and camera panels. Packaging is stubbed.
 */

#include "parhelion_panel.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "../../core/ui/components/picker/ui_picker_component.h"
#include "../../core/ui/scaling/dpi/ui_dpi_scaling.h"
#include "../../state/editor/catalog.h"
#include "weapon_build.h"
#include "weapon_catalog.h"
#include "weapon_recipe.h"

namespace dawn::tools::parhelion {
namespace {

namespace editor = state::editor;
namespace picker = core::ui::components::picker;
using Labels = std::vector<picker::Item>;

/** Candidate perks for one socket lane, with their labels for the picker. Labels borrow the catalog. */
struct LaneCandidates {
    std::vector<std::uint16_t> plugs;
    Labels labels;
};

constexpr const char* kUnnamedPlug = "Unnamed perk";
constexpr const char* kChooseDonor = "Choose a base weapon";
constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

bool g_initialized{};
WeaponRecipe g_recipe{};
std::string g_status;
std::size_t g_donorPick{};
bool g_listsBuilt{};
Labels g_donorLabels;
std::vector<std::uint16_t> g_statRows;
Labels g_statLabels;
std::array<LaneCandidates, kMaxSockets> g_lanes{};

/** @return The base weapon the recipe names, or null while none is chosen or the catalog is not ready. */
[[nodiscard]] const editor::CatalogItem* donor_item() noexcept {
    if (g_recipe.donorItemHash == 0) {
        return nullptr;
    }
    return catalog::items().find(g_recipe.donorItemHash);
}

/** @return True when the base weapon carries a stat row, which the author may then remove. */
[[nodiscard]] bool donor_has_stat(const editor::CatalogItem* donor, std::uint16_t row) noexcept {
    if (donor == nullptr) {
        return false;
    }
    const std::size_t count = std::min<std::size_t>(donor->detail.statCount, donor->detail.stats.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto& stat = donor->detail.stats[index];
        if (stat.row != state::build_data::items::details::kEmptyStatRow &&
            static_cast<std::uint16_t>(stat.row) == row) {
            return true;
        }
    }
    return false;
}

/** @return The localized stat name, or a numbered fallback written into `fallback`. */
[[nodiscard]] const char* stat_name(const editor::Catalog& cat, std::uint16_t row,
                                    std::array<char, 32>& fallback) noexcept {
    const auto found = cat.statNames.find(row);
    if (found != cat.statNames.end()) {
        return found->second.c_str();
    }
    (void)std::snprintf(fallback.data(), fallback.size(), "Stat %u", static_cast<unsigned>(row));
    return fallback.data();
}

/** @return The position of a stat row in the picker list, or zero when it is not listed. */
[[nodiscard]] std::size_t stat_pick_index(std::uint16_t row) noexcept {
    const auto found = std::find(g_statRows.begin(), g_statRows.end(), row);
    return found == g_statRows.end() ? 0 : static_cast<std::size_t>(found - g_statRows.begin());
}

/** Removes a row from the list of base-weapon stats the author removed. */
void forget_removed(std::uint16_t row) noexcept {
    auto begin = g_recipe.removedStats.begin();
    auto end = begin + static_cast<std::ptrdiff_t>(g_recipe.removedStatCount);
    auto found = std::find(begin, end, row);
    if (found == end) {
        return;
    }
    std::move(found + 1, end, found);
    --g_recipe.removedStatCount;
}

/** Records that the author removed a base-weapon stat row. */
void remember_removed(std::uint16_t row) noexcept {
    const auto begin = g_recipe.removedStats.begin();
    const auto end = begin + static_cast<std::ptrdiff_t>(g_recipe.removedStatCount);
    if (std::find(begin, end, row) != end || g_recipe.removedStatCount == kMaxRemovedStats) {
        return;
    }
    g_recipe.removedStats[g_recipe.removedStatCount++] = row;
}

/** Fills each socket lane's candidate perks from the base weapon's own perk pool. */
void refresh_lanes() {
    for (LaneCandidates& lane : g_lanes) {
        lane.plugs.clear();
        lane.labels.clear();
    }
    const editor::CatalogItem* donor = donor_item();
    if (donor == nullptr) {
        return;
    }
    const editor::Catalog& cat = catalog::items();
    for (std::size_t lane = 0; lane < g_recipe.socketCount && lane < kMaxSockets; ++lane) {
        LaneCandidates& candidates = g_lanes[lane];
        candidates.plugs = cat.candidates(*donor, lane, editor::PlugScope::compatible);
        for (const std::uint16_t plug : candidates.plugs) {
            const editor::CatalogItem* item = cat.index(plug);
            candidates.labels.push_back(picker::Item{item != nullptr ? item->name.c_str() : kUnnamedPlug});
        }
    }
}

/** Builds the picker lists once the catalog is ready. They are cleared again on shutdown. */
void build_lists_when_ready() {
    if (g_listsBuilt || catalog::phase() != catalog::Phase::ready) {
        return;
    }
    const editor::Catalog& cat = catalog::items();
    g_donorLabels.clear();
    for (const std::size_t index : catalog::weapons()) {
        g_donorLabels.push_back(picker::Item{cat.items[index].name.c_str()});
    }
    g_statRows.clear();
    for (const auto& entry : cat.statNames) {
        g_statRows.push_back(entry.first);
    }
    std::sort(g_statRows.begin(), g_statRows.end());
    g_statLabels.clear();
    for (const std::uint16_t row : g_statRows) {
        g_statLabels.push_back(picker::Item{cat.statNames.at(row).c_str()});
    }
    refresh_lanes();
    g_listsBuilt = true;
}

/** Makes the picked weapon the recipe's base and resets what depends on it. */
void choose_donor(std::size_t pick) {
    const std::vector<std::size_t>& weapons = catalog::weapons();
    if (pick >= weapons.size()) {
        return;
    }
    const editor::Catalog& cat = catalog::items();
    const editor::CatalogItem& donor = cat.items[weapons[pick]];
    catalog::seed_from_donor(g_recipe, donor, cat);
    g_donorPick = pick;
    refresh_lanes();
    g_status = "Base weapon set to " + donor.name + ". Its stats and perks are the starting point.";
}

/** Changes one stat row to another, refusing a row the weapon already has. */
void set_stat_row(std::size_t index, std::uint16_t row, const editor::CatalogItem* donor) {
    for (std::size_t other = 0; other < g_recipe.statCount; ++other) {
        if (other != index && g_recipe.stats[other].definitionIndex == row) {
            g_status = "That stat is already on this weapon.";
            return;
        }
    }
    StatOverride& stat = g_recipe.stats[index];
    if (stat.definitionIndex == row) {
        return;
    }
    if (donor_has_stat(donor, stat.definitionIndex)) {
        remember_removed(stat.definitionIndex);
    }
    forget_removed(row);
    stat.definitionIndex = row;
    g_recipe.statsEdited = true;
}

/** Removes one stat row, remembering it when the base weapon carries it. */
void remove_stat(std::size_t index, const editor::CatalogItem* donor) {
    const std::uint16_t row = g_recipe.stats[index].definitionIndex;
    if (donor_has_stat(donor, row)) {
        remember_removed(row);
    }
    for (std::size_t next = index; next + 1 < g_recipe.statCount; ++next) {
        g_recipe.stats[next] = g_recipe.stats[next + 1];
    }
    --g_recipe.statCount;
    g_recipe.statsEdited = true;
}

/** Adds the first listed stat row the weapon does not have yet, at a raw value of zero. */
void add_stat() {
    if (g_recipe.statCount >= kMaxStats) {
        g_status = "This weapon already has the most stat rows.";
        return;
    }
    for (const std::uint16_t row : g_statRows) {
        const auto begin = g_recipe.stats.begin();
        const auto end = begin + static_cast<std::ptrdiff_t>(g_recipe.statCount);
        const bool used = std::any_of(begin, end, [row](const StatOverride& stat) {
            return stat.definitionIndex == row;
        });
        if (used) {
            continue;
        }
        g_recipe.stats[g_recipe.statCount++] = StatOverride{row, 0};
        forget_removed(row);
        g_recipe.statsEdited = true;
        return;
    }
    g_status = "No other stat rows are available.";
}

void draw_catalog_status() {
    switch (catalog::phase()) {
    case catalog::Phase::idle:
        ImGui::TextDisabled("The item catalog has not loaded yet.");
        break;
    case catalog::Phase::loading:
        ImGui::TextDisabled("Loading the item catalog...");
        break;
    case catalog::Phase::failed:
        ImGui::TextWrapped("%s", catalog::failure().c_str());
        if (ImGui::Button("Retry Catalog Load")) {
            catalog::start();
        }
        break;
    case catalog::Phase::ready:
        break;
    }
}

void draw_identity() {
    ImGui::TextUnformatted("Identity");
    ImGui::Separator();
    ImGui::TextUnformatted("Name");
    ImGui::SameLine(core::ui::scaling::dpi::pixels(96.0F));
    ImGui::SetNextItemWidth(core::ui::scaling::dpi::pixels(320.0F));
    (void)ImGui::InputText("##parhelion_name", g_recipe.name.data(), g_recipe.name.size());
    std::string ns;
    std::string nsError;
    if (namespace_for_name(text_view(g_recipe.name), ns, nsError)) {
        ImGui::TextDisabled("Namespace %s", ns.c_str());
    } else {
        ImGui::TextWrapped("%s", nsError.c_str());
    }
    ImGui::SetNextItemWidth(core::ui::scaling::dpi::pixels(320.0F));
    (void)ImGui::InputTextWithHint("##parhelion_flavor", "Flavor text", g_recipe.flavor.data(),
                                   g_recipe.flavor.size());
    ImGui::SetNextItemWidth(core::ui::scaling::dpi::pixels(320.0F));
    (void)ImGui::InputTextWithHint("##parhelion_source", "Source", g_recipe.source.data(),
                                   g_recipe.source.size());

    if (ImGui::CollapsingHeader("Identity Hashes")) {
        ImGui::TextWrapped("Parhelion derives these from the namespace. This port does not derive them, "
                           "so enter canonical values such as 0x89ABCDEF.");
        for (std::size_t index = 0; index < kIdentityHashCount; ++index) {
            ImGui::PushID(static_cast<int>(index));
            const std::string_view label = kIdentityLabels[index];
            ImGui::TextUnformatted(label.data(), label.data() + label.size());
            ImGui::SameLine(core::ui::scaling::dpi::pixels(96.0F));
            ImGui::SetNextItemWidth(core::ui::scaling::dpi::pixels(160.0F));
            (void)ImGui::InputText("##hash", g_recipe.identity[index].data(), g_recipe.identity[index].size());
            ImGui::PopID();
        }
    }
}

void draw_donor() {
    ImGui::TextUnformatted("Base Weapon");
    ImGui::Separator();
    ImGui::TextWrapped("The stock weapon whose firing behavior, stats and perks this recipe starts from.");
    const bool ready = catalog::phase() == catalog::Phase::ready;
    const char* preview = g_recipe.donorName[0] != '\0' ? g_recipe.donorName.data() : kChooseDonor;
    std::size_t pick = g_donorPick;
    ImGui::BeginDisabled(!ready);
    if (picker::control("##parhelion_donor", preview, g_donorLabels, pick)) {
        choose_donor(pick);
    }
    ImGui::EndDisabled();
}

void draw_stats(const editor::CatalogItem* donor, bool ready) {
    ImGui::TextUnformatted("Weapon Stats");
    ImGui::Separator();
    ImGui::TextWrapped("Raw values are what the recipe writes. Parhelion's preview shows the in-game "
                       "value, such as RPM, which this port does not compute.");
    if (donor == nullptr) {
        ImGui::TextDisabled("Choose a base weapon to edit its stats.");
        return;
    }

    const editor::Catalog& cat = catalog::items();
    std::array<char, 32> fallback{};
    std::size_t removeIndex = kNoIndex;
    if (ImGui::BeginTable("##parhelion_stats", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Stat", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Raw Value", ImGuiTableColumnFlags_WidthFixed,
                                core::ui::scaling::dpi::pixels(120.0F));
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed,
                                core::ui::scaling::dpi::pixels(72.0F));
        ImGui::TableHeadersRow();
        for (std::size_t index = 0; index < g_recipe.statCount; ++index) {
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            const std::uint16_t row = g_recipe.stats[index].definitionIndex;
            std::size_t pick = stat_pick_index(row);
            ImGui::BeginDisabled(!ready);
            if (picker::control("##stat_row", stat_name(cat, row, fallback), g_statLabels, pick) &&
                pick < g_statRows.size()) {
                set_stat_row(index, g_statRows[pick], donor);
            }
            ImGui::EndDisabled();

            ImGui::TableNextColumn();
            int value = g_recipe.stats[index].value;
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::InputInt("##stat_value", &value)) {
                g_recipe.stats[index].value = value;
                g_recipe.statsEdited = true;
            }

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Remove")) {
                removeIndex = index;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeIndex != kNoIndex) {
        remove_stat(removeIndex, donor);
    }

    ImGui::BeginDisabled(!ready || g_recipe.statCount >= kMaxStats);
    if (ImGui::Button("Add Stat")) {
        add_stat();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!g_recipe.statsEdited && g_recipe.removedStatCount == 0);
    if (ImGui::Button("Reset Stats")) {
        catalog::reset_stats(g_recipe, *donor);
    }
    ImGui::EndDisabled();
}

void draw_sockets(const editor::CatalogItem* donor) {
    ImGui::TextUnformatted("Perks and Sockets");
    ImGui::Separator();
    ImGui::TextWrapped("Each socket holds perks, and its first perk starts equipped. Sockets you do not "
                       "change keep the base weapon's perks.");
    if (donor == nullptr || g_recipe.socketCount == 0) {
        ImGui::TextDisabled("Choose a base weapon to edit its perks.");
        return;
    }

    const editor::Catalog& cat = catalog::items();
    for (std::size_t lane = 0; lane < g_recipe.socketCount; ++lane) {
        SocketOverride& socket = g_recipe.sockets[lane];
        ImGui::PushID(static_cast<int>(lane));
        if (socket.hasSocketType) {
            ImGui::Text("Socket %u (type %u)", static_cast<unsigned>(lane + 1),
                        static_cast<unsigned>(socket.socketType));
        } else {
            ImGui::Text("Socket %u", static_cast<unsigned>(lane + 1));
        }
        if (socket.edited) {
            ImGui::SameLine();
            ImGui::TextDisabled("Edited");
        }

        std::size_t removeChoice = kNoIndex;
        for (std::size_t choice = 0; choice < socket.choiceCount; ++choice) {
            ImGui::PushID(static_cast<int>(choice));
            const editor::CatalogItem* plug = cat.index(socket.choices[choice].plugIndex);
            ImGui::TextUnformatted(plug != nullptr ? plug->name.c_str() : kUnnamedPlug);
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                removeChoice = choice;
            }
            ImGui::PopID();
        }
        if (removeChoice != kNoIndex) {
            for (std::size_t next = removeChoice; next + 1 < socket.choiceCount; ++next) {
                socket.choices[next] = socket.choices[next + 1];
            }
            --socket.choiceCount;
            socket.edited = true;
        }

        LaneCandidates& candidates = g_lanes[lane];
        std::size_t pick = 0;
        ImGui::BeginDisabled(socket.choiceCount >= kMaxChoices || candidates.labels.empty());
        if (picker::control("##add_perk", "Add Perk", candidates.labels, pick) &&
            pick < candidates.plugs.size() && socket.choiceCount < kMaxChoices) {
            const std::uint16_t plug = candidates.plugs[pick];
            if (const editor::CatalogItem* item = cat.index(plug)) {
                socket.choices[socket.choiceCount++] = SocketChoice{item->definition.definitionHash, plug};
                socket.edited = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!socket.edited);
        if (ImGui::Button("Reset Socket")) {
            catalog::reset_socket(g_recipe, *donor, cat, lane);
        }
        ImGui::EndDisabled();
        ImGui::Spacing();
        ImGui::PopID();
    }
}

void draw_ammo_and_damage() {
    ImGui::TextUnformatted("Ammo and Damage");
    ImGui::Separator();
    ImGui::TextWrapped("Donor Default keeps the base weapon's own choice. Changing ammo does not rebalance "
                       "magazine or reserve stats.");
    int ammo = static_cast<int>(g_recipe.ammo);
    ImGui::TextUnformatted("Ammo Type");
    ImGui::SameLine(core::ui::scaling::dpi::pixels(140.0F));
    ImGui::SetNextItemWidth(core::ui::scaling::dpi::pixels(200.0F));
    if (ImGui::Combo("##parhelion_ammo", &ammo, "Donor Default\0Primary\0Special\0Heavy\0")) {
        g_recipe.ammo = static_cast<AmmoOverride>(ammo);
    }
    int damage = static_cast<int>(g_recipe.damage);
    ImGui::TextUnformatted("Damage Type");
    ImGui::SameLine(core::ui::scaling::dpi::pixels(140.0F));
    ImGui::SetNextItemWidth(core::ui::scaling::dpi::pixels(200.0F));
    if (ImGui::Combo("##parhelion_damage", &damage, "Donor Default\0Kinetic\0Arc\0Solar\0Void\0")) {
        g_recipe.damage = static_cast<DamageOverride>(damage);
    }
}

void draw_actions() {
    ImGui::Separator();
    if (ImGui::Button("Check Recipe")) {
        std::string json;
        std::string error;
        if (check_recipe(g_recipe, json, error)) {
            g_status = "Recipe checks out. Save it to keep it.";
        } else {
            g_status = error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Build & Stage")) {
        std::string error;
        (void)build_package_stub(g_recipe, error);
        g_status = error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Recipe")) {
        std::string savedPath;
        std::string error;
        if (save_recipe(g_recipe, savedPath, error)) {
            g_status = "Saved " + savedPath;
        } else {
            g_status = error;
        }
    }
    if (!g_status.empty()) {
        ImGui::TextWrapped("%s", g_status.c_str());
    }
}

} // namespace

void draw() noexcept {
    if (!g_initialized) {
        g_recipe = make_blank_recipe();
        g_initialized = true;
    }
    if (catalog::phase() == catalog::Phase::idle) {
        catalog::start();
    }
    build_lists_when_ready();

    ImGui::TextUnformatted("Weapon");
    ImGui::Separator();
    ImGui::TextWrapped("Build a custom weapon from a stock base weapon. Save writes its recipe file. "
                       "Packaging into the game is not ported yet.");
    draw_catalog_status();
    ImGui::Spacing();
    draw_identity();
    ImGui::Spacing();
    draw_donor();
    ImGui::Spacing();

    const editor::CatalogItem* donor = donor_item();
    draw_stats(donor, catalog::phase() == catalog::Phase::ready);
    ImGui::Spacing();
    draw_sockets(donor);
    ImGui::Spacing();
    draw_ammo_and_damage();
    ImGui::Spacing();
    draw_actions();
}

void shutdown() noexcept {
    g_donorLabels.clear();
    g_statRows.clear();
    g_statLabels.clear();
    for (LaneCandidates& lane : g_lanes) {
        lane.plugs.clear();
        lane.labels.clear();
    }
    g_listsBuilt = false;
    catalog::stop();
}

} // namespace dawn::tools::parhelion
