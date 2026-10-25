// NOT COMPILED OR TESTED. Stubbed seam between the client randomizer panel and the account editor.
#pragma once

#include "randomizer_model.h"

namespace dawn::client::ui::randomizer::bridge {

/**
 * STUB SEAM. Every function here is a clearly named stub in randomizer_bridge.cpp that reports
 * "not connected". Dawn's real logic lives in the editor layer, and the client panel holds no
 * account draft to pass it. Each comment names the editor function a real implementation would
 * call. No function here is a verified Dawn API beyond those names.
 */

/** @return True when a real editor session backs these calls. Always false in this port. */
[[nodiscard]] bool is_connected() noexcept;

/**
 * Rolls the selected sections of the active character.
 * Real call target: state::editor::randomize(Draft&, Catalog const&, character, slots mask, level,
 * std::mt19937&, error). The Rust options for held inventory and locked items have no equivalent
 * there, so they are carried but not applied by a real session yet.
 * @param options Sections to replace. @param itemPower Power the rolled items are made at.
 */
[[nodiscard]] Outcome randomize_loadout(const LoadoutOptions& options, int itemPower) noexcept;

/**
 * Reads the equipped armor's stat totals, as the character screen adds them.
 * Real call target: state::editor::armor_input(...) followed by summing ArmorInput::current.
 */
[[nodiscard]] Stats equipped_totals() noexcept;

/**
 * Works out the closest armor for the goals, without changing anything.
 * Real call target: state::editor::plan_armor(input, minimums, cancel).
 * @param targets Least of each stat, zero for a stat left free.
 * @param allowInventorySwaps Whether stowed pieces may replace the armor on.
 */
[[nodiscard]] ArmorPreview preview_armor(const Stats& targets, bool allowInventorySwaps) noexcept;

/**
 * Puts the previewed plan on, all or none.
 * Real call target: state::editor::apply_armor_plan(draft, catalog, input, plan, error).
 */
[[nodiscard]] Outcome adjust_armor(const Stats& targets, bool allowInventorySwaps) noexcept;

} // namespace dawn::client::ui::randomizer::bridge
