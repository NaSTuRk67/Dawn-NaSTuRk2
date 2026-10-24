#include "loadout_panel.h"
#include "panoptes_icons.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cctype>
#include <limits>
#include <Windows.h>
#include <imgui.h>

#include "../../../state/account/account_state.h"
#include "../../../state/build_data/runtime.h"
#include "../../../state/runtime/runtime.h"
#include "../../bap/runtime.h"

namespace dawn::server::ui::loadout {
namespace {

namespace inventory = state::account::inventory;

constexpr std::array<const char*, inventory::kEquipmentSlotCount> kSlotNames{
    "Kinetic", "Energy", "Heavy", "Helmet", "Gauntlets", "Chest", "Legs", "Class Item",
    "Ghost", "Vehicle", "Ship", "Subclass", "Clan Banner", "Emblem", "Emote", "Finisher"};

constexpr std::array<const char*, 3> kClassNames{"Titan", "Hunter", "Warlock"};
constexpr std::array<const char*, 3> kRaceNames{"Human", "Awoken", "Exo"};
constexpr std::array<const char*, 2> kGenderNames{"Male", "Female"};

struct SubclassChoice {
    const char* name;
    std::uint32_t hash;
};
constexpr std::array<std::array<SubclassChoice, 3>, 3> kSubclassChoices{{
    {{{"Void — Sentinel", 0xC99B33E9U}, {"Arc — Striker", 0xB0554739U},
      {"Solar — Sunbreaker", 0xB920CE9AU}}},
    {{{"Arc — Arcstrider", 0x4F91DC97U}, {"Solar — Gunslinger", 0xD8B8D1FCU},
      {"Void — Nightstalker", 0xC0483D8BU}}},
    {{{"Solar — Dawnblade", 0xCF88FEA5U}, {"Arc — Stormcaller", 0x686A154AU},
      {"Void — Voidwalker", 0xE7BC88B0U}}},
}};
constexpr std::array<SubclassChoice, 9> kAllSubclassChoices{{
    {"Titan Void — Sentinel / Bubble", 0xC99B33E9U},
    {"Titan Arc — Striker", 0xB0554739U},
    {"Titan Solar — Sunbreaker", 0xB920CE9AU},
    {"Hunter Arc — Arcstrider", 0x4F91DC97U},
    {"Hunter Solar — Gunslinger", 0xD8B8D1FCU},
    {"Hunter Void — Nightstalker", 0xC0483D8BU},
    {"Warlock Solar — Dawnblade", 0xCF88FEA5U},
    {"Warlock Arc — Stormcaller", 0x686A154AU},
    {"Warlock Void — Voidwalker", 0xE7BC88B0U},
}};

struct AttunementChoice {
    const char* name;
    std::uint8_t superEntry;
    std::uint8_t meleeEntry;
};
struct AttunementSet {
    std::uint32_t subclassHash;
    std::array<AttunementChoice, 3> choices;
};
constexpr std::array<AttunementSet, 9> kAttunementSets{{
    {0xC99B33E9U, {{{"Code of the Protector", 10, 11}, {"Code of the Aggressor", 10, 15}, {"Code of the Commander", 10, 21}}}},
    {0xB0554739U, {{{"Code of the Earthshaker", 10, 11}, {"Code of the Juggernaut", 10, 15}, {"Code of the Missile", 20, 21}}}},
    {0xB920CE9AU, {{{"Code of the Fire-Forged", 10, 11}, {"Code of the Siegebreaker", 10, 15}, {"Code of the Devastator", 20, 21}}}},
    {0x4F91DC97U, {{{"Way of the Warrior", 10, 11}, {"Way of the Wind", 10, 15}, {"Way of the Current", 10, 21}}}},
    {0xD8B8D1FCU, {{{"Way of the Outlaw", 10, 11}, {"Way of the Sharpshooter", 10, 15}, {"Way of a Thousand Cuts", 20, 21}}}},
    {0xC0483D8BU, {{{"Way of the Trapper", 10, 11}, {"Way of the Pathfinder", 10, 15}, {"Way of the Wraith", 20, 21}}}},
    {0xCF88FEA5U, {{{"Attunement of Sky", 10, 11}, {"Attunement of Flame", 10, 15}, {"Attunement of Grace", 20, 21}}}},
    {0x686A154AU, {{{"Attunement of Conduction", 10, 11}, {"Attunement of the Elements", 10, 15}, {"Attunement of Control", 20, 21}}}},
    {0xE7BC88B0U, {{{"Attunement of Chaos", 10, 11}, {"Attunement of Hunger", 10, 15}, {"Attunement of Fission", 20, 21}}}},
}};
constexpr std::array<std::array<const char*, 3>, 3> kMovementNames{{
    {"High Lift", "Strafe Lift", "Catapult Lift"},
    {"High Jump", "Strafe Jump", "Triple Jump"},
    {"Strafe Glide", "Burst Glide", "Balanced Glide"},
}};
constexpr std::array<std::array<const char*, 2>, 3> kClassAbilityNames{{
    {"Towering Barricade", "Rally Barricade"},
    {"Marksman's Dodge", "Gambler's Dodge"},
    {"Healing Rift", "Empowering Rift"},
}};
struct GrenadeSet {
    std::uint32_t subclassHash;
    std::array<const char*, 3> names;
};
constexpr std::array<GrenadeSet, 9> kGrenadeSets{{
    {0xC99B33E9U, {"Magnetic Grenade", "Voidwall Grenade", "Suppressor Grenade"}},
    {0xB0554739U, {"Flashbang Grenade", "Pulse Grenade", "Lightning Grenade"}},
    {0xB920CE9AU, {"Incendiary Grenade", "Thermite Grenade", "Fusion Grenade"}},
    {0x4F91DC97U, {"Skip Grenade", "Flux Grenade", "Arcbolt Grenade"}},
    {0xD8B8D1FCU, {"Incendiary Grenade", "Swarm Grenade", "Tripmine Grenade"}},
    {0xC0483D8BU, {"Vortex Grenade", "Spike Grenade", "Voidwall Grenade"}},
    {0xCF88FEA5U, {"Solar Grenade", "Firebolt Grenade", "Fusion Grenade"}},
    {0x686A154AU, {"Arcbolt Grenade", "Pulse Grenade", "Storm Grenade"}},
    {0xE7BC88B0U, {"Vortex Grenade", "Axion Bolt", "Scatter Grenade"}},
}};

[[nodiscard]] const AttunementSet* attunements_for(std::uint32_t hash) noexcept {
    for (const AttunementSet& set : kAttunementSets) {
        if (set.subclassHash == hash) {
            return &set;
        }
    }
    return nullptr;
}

[[nodiscard]] const GrenadeSet* grenades_for(std::uint32_t hash) noexcept {
    for (const GrenadeSet& set : kGrenadeSets) {
        if (set.subclassHash == hash) {
            return &set;
        }
    }
    return nullptr;
}

std::size_t g_character{};
std::array<char, 160> g_status{"Ready"};
bool g_statusError{};
std::array<std::size_t, 9> g_attunementSelections{};
std::array<bool, 9> g_attunementDirty{};

struct PlugEdit {
    std::uint64_t instanceSoid{};
    std::uint8_t lane{};
    std::uint32_t value{};
    bool used{};
};

// One character cannot expose more than 16 * 12 equipped lanes plus the visible inventory lanes.
// The fixed cache keeps partially typed hexadecimal values stable across ImGui frames.
std::array<PlugEdit, 512> g_plugEdits{};

struct ItemEdit {
    std::uint64_t instanceSoid{};
    std::uint32_t value{};
    bool used{};
};

std::array<ItemEdit, inventory::kCharacterItemCapacity + inventory::kEquipmentSlotCount>
    g_itemEdits{};

void status(const char* message, bool error = false) noexcept {
    (void)std::snprintf(g_status.data(), g_status.size(), "%s", message);
    g_statusError = error;
}

// Accept the hexadecimal hashes shown by Dawn as well as decimal hashes copied from
// database websites. An explicit button is used because Destiny's input hook can consume
// Ctrl+V before Dear ImGui receives it.
[[nodiscard]] bool paste_hash(std::uint32_t& value) noexcept {
    const char* text = ImGui::GetClipboardText();
    if (text == nullptr) {
        status("The clipboard does not contain text.", true);
        return false;
    }
    while (std::isspace(static_cast<unsigned char>(*text)) != 0) {
        ++text;
    }
    int base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    } else {
        for (const char* cursor = text; *cursor != '\0' &&
             std::isspace(static_cast<unsigned char>(*cursor)) == 0; ++cursor) {
            if ((*cursor >= 'A' && *cursor <= 'F') || (*cursor >= 'a' && *cursor <= 'f')) {
                base = 16;
                break;
            }
        }
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text, &end, base);
    if (end == text || errno == ERANGE || parsed > std::numeric_limits<std::uint32_t>::max()) {
        status("Clipboard hash is invalid. Copy a decimal hash, 0x hash, or 8-digit hex hash.", true);
        return false;
    }
    while (std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (*end != '\0') {
        status("Clipboard hash is invalid. Copy only the hash number.", true);
        return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    status(base == 16 ? "Hex hash pasted. Click Apply or Replace item."
                      : "Decimal website hash pasted. Click Apply or Replace item.");
    return true;
}

void hash_clipboard_shortcuts(std::uint32_t& value) noexcept {
    if (!ImGui::IsItemActive()) {
        return;
    }
    const bool control = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool paste = control && (GetAsyncKeyState('V') & 0x0001) != 0;
    const bool copy = control && (GetAsyncKeyState('C') & 0x0001) != 0;
    if (paste) {
        // Replace the complete hash so Ctrl+A is unnecessary inside the game overlay.
        (void)paste_hash(value);
    }
    if (copy) {
        char hash[9]{};
        (void)std::snprintf(hash, sizeof(hash), "%08X", value);
        ImGui::SetClipboardText(hash);
        status("Hash copied to the Windows clipboard.");
    }
}

[[nodiscard]] const char* class_name(state::CharacterClass value) noexcept {
    const std::size_t index = static_cast<std::size_t>(value);
    return index < kClassNames.size() ? kClassNames[index] : "Unknown";
}

[[nodiscard]] const char* race_name(state::CharacterRace value) noexcept {
    const std::size_t index = static_cast<std::size_t>(value);
    return index < kRaceNames.size() ? kRaceNames[index] : "Unknown";
}

[[nodiscard]] const char* gender_name(state::CharacterGender value) noexcept {
    const std::size_t index = static_cast<std::size_t>(value);
    return index < kGenderNames.size() ? kGenderNames[index] : "Unknown";
}

PlugEdit& plug_edit(std::uint64_t instanceSoid,
                    std::uint8_t lane,
                    std::uint32_t current) noexcept {
    PlugEdit* available = nullptr;
    for (PlugEdit& edit : g_plugEdits) {
        if (edit.used && edit.instanceSoid == instanceSoid && edit.lane == lane) {
            return edit;
        }
        if (!edit.used && available == nullptr) {
            available = &edit;
        }
    }
    // Capacity is deliberately larger than the visible authored loadout. Reusing the first entry
    // is only a final guard for malformed state and still leaves editing bounded.
    PlugEdit& edit = available != nullptr ? *available : g_plugEdits.front();
    edit = {instanceSoid, lane, current, true};
    return edit;
}

ItemEdit& item_edit(std::uint64_t instanceSoid, std::uint32_t current) noexcept {
    ItemEdit* available = nullptr;
    for (ItemEdit& edit : g_itemEdits) {
        if (edit.used && edit.instanceSoid == instanceSoid) {
            return edit;
        }
        if (!edit.used && available == nullptr) {
            available = &edit;
        }
    }
    ItemEdit& edit = available != nullptr ? *available : g_itemEdits.front();
    edit = {instanceSoid, current, true};
    return edit;
}

void apply_item_definition(const inventory::Item& item, ItemEdit& edit) noexcept {
    if (!state::replace_item_definition(item.instanceSoid, edit.value)) {
        status("Replacement rejected: use an item hash from the same equipment bucket.", true);
        return;
    }
    (void)state::save_account();
    if (server::bap::request_account_refresh()) {
        status("Main item hash replaced; native sockets restored and live refresh queued.");
    } else {
        status("Main item replaced; no active game session was available to refresh.", true);
    }
}

void apply_plug(const inventory::Item& item, std::uint8_t lane, PlugEdit& edit) noexcept {
    state::build_data::items::Definition definition{};
    if (!state::build_data::find_item_definition_hash(edit.value, definition)) {
        status("That plug hash does not exist in the installed game build.", true);
        return;
    }
    state::PendingSocketPlug mutation{};
    if (!state::prepare_socket_plug(item.instanceSoid, lane, definition.definitionIndex, mutation)) {
        if (!state::override_socket_plug(item.instanceSoid, lane, edit.value)) {
            status("Socket override failed; the item or socket lane is unavailable.", true);
            return;
        }
        (void)state::save_account();
        const bool live = server::bap::request_account_refresh();
        status(live ? "Incompatible plug forced; live refresh queued."
                    : "Incompatible plug forced; no active session was available to refresh.",
               !live);
        return;
    }
    if (!state::commit_socket_plug(mutation)) {
        status("The item changed before the socket edit could commit.", true);
        return;
    }
    (void)state::save_account();
    if (server::bap::request_account_refresh()) {
        status("Plug applied and live refresh queued.");
    } else {
        status("Plug applied; no active game session was available to refresh.", true);
    }
}

void draw_plugs(const inventory::Item& item, bool enabled) noexcept {
    state::build_data::items::Definition itemDefinition{};
    state::build_data::items::details::Definition itemDetail{};
    const bool hasNativeLayout =
        state::build_data::find_item_definition_hash(item.definitionHash, itemDefinition)
        && state::build_data::find_configured_item_detail(itemDefinition.definitionIndex,
                                                          itemDetail);
    inventory::Sockets displayed = item.sockets;
    const bool nativeDefaults = displayed.policy == inventory::SocketPolicy::nativeDefaults;
    if (nativeDefaults) {
        displayed = {};
        displayed.policy = inventory::SocketPolicy::authored;
        if (hasNativeLayout) {
            displayed.plugCount = itemDetail.ordinarySocketCount;
            for (std::size_t lane = 0; lane < displayed.plugCount; ++lane) {
                const std::uint16_t nativeIndex = itemDetail.initialPlugIndices[lane];
                state::build_data::items::Definition nativePlug{};
                if (nativeIndex != state::build_data::items::details::kUnavailableItemIndex
                    && state::build_data::find_item_definition_index(nativeIndex, nativePlug)) {
                    displayed.plugs[lane] = nativePlug.definitionHash;
                }
            }
        }
    }
    if (nativeDefaults) {
        ImGui::Text("Native plugs: %zu (Apply materializes the edited lane)",
                    displayed.plugCount);
    } else {
        ImGui::Text("Authored plugs: %zu", displayed.plugCount);
    }
    for (std::size_t lane = 0; lane < displayed.plugCount; ++lane) {
        ImGui::PushID(static_cast<int>(lane));
        const std::uint32_t current = displayed.plugs[lane].value_or(0);
        PlugEdit& edit = plug_edit(item.instanceSoid, static_cast<std::uint8_t>(lane), current);
        if (const ImTextureID icon = icons::texture(current); icon != ImTextureID{}) {
            ImGui::Image(icon, ImVec2(44.0F, 44.0F));
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        const std::string_view plugName = icons::name(current);
        std::string_view nativeName{};
        if (hasNativeLayout && lane < itemDetail.initialPlugIndices.size()) {
            const std::uint16_t nativeIndex = itemDetail.initialPlugIndices[lane];
            state::build_data::items::Definition nativePlug{};
            if (nativeIndex != state::build_data::items::details::kUnavailableItemIndex
                && state::build_data::find_item_definition_index(nativeIndex, nativePlug)) {
                nativeName = icons::name(nativePlug.definitionHash);
            }
        }
        const char* category = nullptr;
        const std::string_view categoryName = nativeName.empty() ? plugName : nativeName;
        if (categoryName.find("Ornament") != std::string_view::npos) {
            category = "Ornament";
        } else if (categoryName.find("Shader") != std::string_view::npos) {
            category = "Shader";
        } else if (categoryName.find("Catalyst") != std::string_view::npos) {
            category = "Catalyst";
        } else if (categoryName.find("Mod Socket") != std::string_view::npos
                   || categoryName.find("Mod") != std::string_view::npos) {
            category = "Mod";
        } else if (!plugName.empty()) {
            category = "Perk";
        }
        if (category != nullptr) {
            ImGui::Text("%s — Socket %zu — %.*s", category, lane + 1,
                        static_cast<int>(plugName.size()), plugName.data());
        } else if (!plugName.empty()) {
            ImGui::Text("Socket %zu — %.*s", lane + 1,
                        static_cast<int>(plugName.size()), plugName.data());
        } else {
            ImGui::Text("Socket %zu", lane + 1);
        }
        ImGui::SetNextItemWidth(115.0F);
        ImGui::BeginDisabled(!enabled);
        (void)ImGui::InputScalar("##plug_hash",
                                 ImGuiDataType_U32,
                                 &edit.value,
                                 nullptr,
                                 nullptr,
                                 "%08X",
                                 ImGuiInputTextFlags_CharsHexadecimal
                                     | ImGuiInputTextFlags_CharsUppercase);
        hash_clipboard_shortcuts(edit.value);
        ImGui::SameLine();
        if (ImGui::SmallButton("Paste")) {
            (void)paste_hash(edit.value);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Apply")) {
            apply_plug(item, static_cast<std::uint8_t>(lane), edit);
        }
        ImGui::EndDisabled();
        ImGui::EndGroup();
        ImGui::PopID();
    }
}

void equipment_action(const inventory::Item& item, bool equipped, bool enabled) noexcept {
    ImGui::BeginDisabled(!enabled);
    state::PendingEquipmentSwap mutation{};
    const char* label = equipped ? "Unequip" : "Equip";
    if (ImGui::SmallButton(label)) {
        const bool prepared = equipped ? state::prepare_equipment_unequip(item.instanceSoid, mutation)
                                       : state::prepare_equipment_swap(item.instanceSoid, mutation);
        if (!prepared) {
            status("The loadout change was rejected by NaSTuRk validation.", true);
        } else if (!state::commit_equipment_swap(mutation)) {
            status("The loadout changed before the transaction could commit.", true);
        } else {
            (void)state::save_account();
            const bool live = server::bap::request_account_refresh();
            status(live ? (equipped ? "Item moved; live refresh queued."
                                    : "Item equipped; live refresh queued.")
                        : "Item changed; no active game session was available to refresh.",
                   !live);
        }
    }
    ImGui::EndDisabled();
}

void draw_item(const inventory::Item& item, bool equipped, bool actionEnabled) noexcept {
    if (const ImTextureID icon = icons::texture(item.definitionHash); icon != ImTextureID{}) {
        ImGui::Image(icon, ImVec2(72.0F, 72.0F));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::Text("Level %d  |  Qty %d", item.level, item.quantity);
    ImGui::TextDisabled("Instance %016llX", static_cast<unsigned long long>(item.instanceSoid));
    ItemEdit& edit = item_edit(item.instanceSoid, item.definitionHash);
    ImGui::BeginDisabled(!actionEnabled);
    ImGui::SetNextItemWidth(115.0F);
    (void)ImGui::InputScalar("##item_hash",
                             ImGuiDataType_U32,
                             &edit.value,
                             nullptr,
                             nullptr,
                             "%08X",
                             ImGuiInputTextFlags_CharsHexadecimal
                                 | ImGuiInputTextFlags_CharsUppercase);
    hash_clipboard_shortcuts(edit.value);
    ImGui::SameLine();
    if (ImGui::SmallButton("Paste")) {
        (void)paste_hash(edit.value);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Replace item")) {
        apply_item_definition(item, edit);
    }
    ImGui::EndDisabled();
    equipment_action(item, equipped, actionEnabled);
    ImGui::EndGroup();
    ImGui::Spacing();
    draw_plugs(item, actionEnabled);
}

void apply_ability_tuple(std::uint8_t movement,
                         std::uint8_t grenade,
                         std::uint8_t superEntry,
                         std::uint8_t melee,
                         std::uint8_t classAbility) noexcept {
    if (!state::set_subclass_abilities(movement, grenade, superEntry, melee, classAbility)) {
        status("Ability combination rejected; reload saved data and try again.", true);
        return;
    }
    (void)state::save_account();
    const bool live = server::bap::request_account_refresh();
    status(live ? "Ability changed and refreshed live."
                : "Ability saved; no active session was available to refresh.",
           !live);
}

void draw_subclass_selector(const state::CharacterState& character,
                            const inventory::Item& item) noexcept {
    ItemEdit& edit = item_edit(item.instanceSoid, item.definitionHash);
    const auto& choices = kSubclassChoices[static_cast<std::size_t>(character.characterClass)];
    const char* preview = "Choose element / Super";
    for (const SubclassChoice& choice : choices) {
        if (choice.hash == edit.value) {
            preview = choice.name;
        }
    }
    ImGui::SeparatorText("Subclass / Super");
    ImGui::BeginDisabled(!character.selected);
    ImGui::SetNextItemWidth(230.0F);
    if (ImGui::BeginCombo("##subclass_super", preview)) {
        for (const SubclassChoice& choice : choices) {
            if (ImGui::Selectable(choice.name, edit.value == choice.hash)) {
                edit.value = choice.hash;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Apply Subclass")) {
        apply_item_definition(item, edit);
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Applies the element and resets all ability selectors together.");

    const char* crossPreview = "Choose any class subclass";
    for (const SubclassChoice& choice : kAllSubclassChoices) {
        if (choice.hash == edit.value) {
            crossPreview = choice.name;
            break;
        }
    }
    ImGui::SetNextItemWidth(300.0F);
    ImGui::BeginDisabled(!character.selected);
    if (ImGui::BeginCombo("Experimental Cross-Class", crossPreview)) {
        for (const SubclassChoice& choice : kAllSubclassChoices) {
            if (ImGui::Selectable(choice.name, edit.value == choice.hash)) {
                edit.value = choice.hash;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Apply Cross-Class")) {
        apply_item_definition(item, edit);
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Unsafe test: mismatched animations may disconnect. Switch back here if needed.");

    const AttunementSet* paths = attunements_for(edit.value);
    if (paths == nullptr) {
        return;
    }
    const std::size_t setIndex = static_cast<std::size_t>(paths - kAttunementSets.data());
    std::size_t& selectedPath = g_attunementSelections[setIndex];
    if (!g_attunementDirty[setIndex]) {
        selectedPath = 0;
        for (std::size_t index = 0; index < paths->choices.size(); ++index) {
            const AttunementChoice& choice = paths->choices[index];
            if (choice.superEntry == character.superAbilityEntry
                && choice.meleeEntry == character.meleeAbilityEntry) {
                selectedPath = index;
                break;
            }
        }
    }
    ImGui::SetNextItemWidth(230.0F);
    if (ImGui::BeginCombo("##subclass_attunement", paths->choices[selectedPath].name)) {
        for (std::size_t index = 0; index < paths->choices.size(); ++index) {
            if (ImGui::Selectable(paths->choices[index].name, selectedPath == index)) {
                selectedPath = index;
                g_attunementDirty[setIndex] = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!character.selected);
    if (ImGui::SmallButton("Apply Path")) {
        const AttunementChoice& choice = paths->choices[selectedPath];
        if (!state::set_subclass_attunement(choice.superEntry, choice.meleeEntry)) {
            status("Subclass path rejected; apply the subclass first and try again.", true);
        } else {
            (void)state::save_account();
            const bool live = server::bap::request_account_refresh();
            g_attunementDirty[setIndex] = false;
            status(live ? "Subclass path applied; Super and melee refreshed live."
                        : "Subclass path saved; no active session was available to refresh.",
                   !live);
        }
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Changes the linked Super variant and melee path together.");

    const std::size_t classIndex = static_cast<std::size_t>(character.characterClass);
    const GrenadeSet* grenadeSet = grenades_for(edit.value);
    if (classIndex >= kMovementNames.size() || grenadeSet == nullptr) {
        return;
    }
    ImGui::SeparatorText("Movement / Grenade / Class Ability");
    const std::size_t movementIndex = character.movementAbilityEntry >= 4
        && character.movementAbilityEntry <= 6 ? character.movementAbilityEntry - 4 : 0;
    ImGui::SetNextItemWidth(230.0F);
    if (ImGui::BeginCombo("Jump / Lift / Glide", kMovementNames[classIndex][movementIndex])) {
        for (std::size_t index = 0; index < 3; ++index) {
            if (ImGui::Selectable(kMovementNames[classIndex][index], index == movementIndex)) {
                apply_ability_tuple(static_cast<std::uint8_t>(4 + index),
                                    character.grenadeAbilityEntry,
                                    character.superAbilityEntry,
                                    character.meleeAbilityEntry,
                                    character.classAbilityEntry);
            }
        }
        ImGui::EndCombo();
    }
    const std::size_t grenadeIndex = character.grenadeAbilityEntry >= 7
        && character.grenadeAbilityEntry <= 9 ? character.grenadeAbilityEntry - 7 : 0;
    ImGui::SetNextItemWidth(230.0F);
    if (ImGui::BeginCombo("Grenade", grenadeSet->names[grenadeIndex])) {
        for (std::size_t index = 0; index < 3; ++index) {
            if (ImGui::Selectable(grenadeSet->names[index], index == grenadeIndex)) {
                apply_ability_tuple(character.movementAbilityEntry,
                                    static_cast<std::uint8_t>(7 + index),
                                    character.superAbilityEntry,
                                    character.meleeAbilityEntry,
                                    character.classAbilityEntry);
            }
        }
        ImGui::EndCombo();
    }
    const std::size_t abilityIndex = character.classAbilityEntry == 3 ? 1 : 0;
    ImGui::SetNextItemWidth(230.0F);
    if (ImGui::BeginCombo("Class Ability", kClassAbilityNames[classIndex][abilityIndex])) {
        for (std::size_t index = 0; index < 2; ++index) {
            if (ImGui::Selectable(kClassAbilityNames[classIndex][index], index == abilityIndex)) {
                apply_ability_tuple(character.movementAbilityEntry,
                                    character.grenadeAbilityEntry,
                                    character.superAbilityEntry,
                                    character.meleeAbilityEntry,
                                    static_cast<std::uint8_t>(2 + index));
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("Selections save automatically and refresh the active character live.");
}

void draw_character(const state::CharacterState& character) noexcept {
    ImGui::Text("%s  |  %s %s  |  Level %u",
                class_name(character.characterClass),
                gender_name(character.gender),
                race_name(character.race),
                static_cast<unsigned>(character.level));
    ImGui::SameLine();
    if (character.selected) {
        ImGui::TextColored(ImVec4(0.35F, 0.85F, 0.45F, 1.0F), "Selected in game");
    } else {
        ImGui::TextDisabled("View only (select this character in game to edit)");
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Equipment");
    ImGui::Separator();
    for (std::size_t slot = 0; slot < inventory::kEquipmentSlotCount; ++slot) {
        ImGui::PushID(static_cast<int>(slot));
        const auto& item = character.equipment.slots[slot];
        const bool open = ImGui::TreeNodeEx(kSlotNames[slot], ImGuiTreeNodeFlags_SpanAvailWidth,
                                            "%s%s", kSlotNames[slot], item ? "" : " — Empty");
        if (open) {
            if (item) {
                draw_item(*item, true, character.selected);
                if (slot == static_cast<std::size_t>(inventory::EquipmentSlot::subclass)) {
                    draw_subclass_selector(character, *item);
                }
            } else {
                ImGui::TextDisabled("No item equipped.");
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Text("Inventory (%zu)", character.inventory.count);
    ImGui::Separator();
    if (character.inventory.count == 0) {
        ImGui::TextDisabled("No unequipped character items.");
    }
    for (std::size_t index = 0; index < character.inventory.count; ++index) {
        const inventory::Item& item = character.inventory.values[index];
        ImGui::PushID(static_cast<int>(inventory::kEquipmentSlotCount + index));
        char label[80]{};
        (void)std::snprintf(label, sizeof(label), "#%zu  %08X", index + 1, item.definitionHash);
        if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth)) {
            draw_item(item, false, character.selected);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

} // namespace

void draw() noexcept {
    const state::AccountState account = state::account_snapshot();
    ImGui::TextUnformatted("Panoptes Loadout (experimental port)");
    ImGui::TextDisabled("NaSTuRk");
    ImGui::Separator();
    ImGui::TextWrapped("Live Dawn equipment and inventory. Equip and Unequip use Dawn's "
                       "validated account transactions; select a character in game before editing.");
    if (ImGui::SmallButton("Save Loadout")) {
        const bool saved = state::save_account();
        status(saved ? "Loadout saved; it will restore automatically next launch."
                     : "Could not write the loadout save file.",
               !saved);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Load Saved Data / Reattach")) {
        const bool loaded = state::load_account();
        const bool refreshed = loaded && server::bap::request_account_refresh();
        status(refreshed ? "Saved loadout reattached; live refresh queued."
                         : loaded ? "Save loaded, but no active game session could refresh."
                                  : "Could not load a valid saved loadout.",
               !refreshed);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Changes made here are also auto-saved.");
    ImGui::Spacing();

    if (account.characterCount == 0) {
        ImGui::TextDisabled("No account characters are loaded yet.");
        return;
    }
    if (g_character >= account.characterCount) {
        g_character = 0;
    }
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        if (index != 0) {
            ImGui::SameLine();
        }
        char label[48]{};
        (void)std::snprintf(label,
                            sizeof(label),
                            "%s %zu%s",
                            class_name(account.characters[index].characterClass),
                            index + 1,
                            account.characters[index].selected ? " *" : "");
        if (ImGui::Selectable(label, g_character == index, 0, ImVec2(110.0F, 0.0F))) {
            g_character = index;
        }
    }

    if (g_statusError) {
        ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.35F, 1.0F), "%s", g_status.data());
    } else {
        ImGui::TextDisabled("%s", g_status.data());
    }
    ImGui::Spacing();
    draw_character(account.characters[g_character]);
}

void shutdown() noexcept {
    icons::shutdown();
}

} // namespace dawn::server::ui::loadout
