// NOT COMPILED OR TESTED. Ported from Sundial's app/equipment/randomize and armor_stats_adjuster.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace dawn::client::ui::randomizer {

/** Six character stats in the order the armor stat targets use, matching the editor's `Stats`. */
inline constexpr std::size_t kStatCount = 6;
using Stats = std::array<int, kStatCount>;
inline constexpr std::array<const char*, kStatCount> kStatNames{
    "Mobility", "Resilience", "Recovery", "Discipline", "Intellect", "Strength"};

/** Highest armor stat target the sliders offer. Matches the editor's kStatCap (armor_plan.h). */
inline constexpr int kStatTargetMaximum = 100;

/** Highest Power the randomizer field offers, as in the Core loadout randomizer. */
inline constexpr int kItemPowerMaximum = 15000;

/** Character equipment slot names in the editor's slot order (edit.h kSlots). */
inline constexpr std::size_t kEquipmentSlotCount = 16;
inline constexpr std::size_t kFirstArmorSlot = 3;   // Helmet
inline constexpr std::size_t kLastArmorSlot = 7;    // Class Item
inline constexpr std::size_t kSubclassSlot = 11;    // Subclass
inline constexpr std::size_t kLastWeaponSlot = 2;   // Power

/**
 * Which sections a loadout roll replaces. Mirrors the Rust LoadoutOptions. The defaults match the
 * Rust defaults: nothing selected, locked items kept, held inventory not replaced.
 */
struct LoadoutOptions {
    bool weapons{};
    bool armor{};
    bool equipmentFlair{};
    bool subclass{};
    bool replaceHeldInventory{};
    bool keepLockedItems{true};

    /** @return True when at least one section is selected. */
    [[nodiscard]] bool any() const noexcept { return weapons || armor || equipmentFlair || subclass; }
};

/** One line of status text shown under a panel, failed or not. */
struct Feedback {
    std::string text;
    bool failed{};
};

/** Result of one bridge call that changes the account. */
struct Outcome {
    bool ok{};
    std::string message;
};

/**
 * What the armor stat preview reports for the current targets. Mirrors the parts of the Rust
 * Solution that the summary line and the totals read. The per-piece table is not carried.
 */
struct ArmorPreview {
    /** False when equipped armor could not be read, which disables the preview. */
    bool available{};
    /** True when every goal can be met. */
    bool exact{};
    Stats current{};
    Stats projected{};
    /** How far each stat falls short of its goal, counted up to the cap. */
    Stats shortfalls{};
    unsigned pieces{};
    unsigned swaps{};
    unsigned masterworks{};
    unsigned plugs{};

    /** @return True when adjusting the armor would change something. */
    [[nodiscard]] bool changes() const noexcept { return pieces != 0 || swaps != 0; }
};

/** Everything the panel keeps between frames. Lives for the process, as the other panels' state does. */
struct PanelState {
    LoadoutOptions loadout{};
    /** The Rust default: unlocked armor in inventory may be swapped in. */
    bool allowInventorySwaps{true};
    /** Power a rolled item is made at, shown as the Core loadout randomizer shows it. */
    int itemPower{};
    Stats targets{};
    ArmorPreview preview{};
    Feedback feedback{};
};

} // namespace dawn::client::ui::randomizer
