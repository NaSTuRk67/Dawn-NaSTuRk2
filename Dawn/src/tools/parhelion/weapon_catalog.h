#pragma once

/**
 * The stock item catalog the weapon editor reads its base weapons and perks from.
 *
 * This reuses Dawn's own editor catalog (state/editor/catalog.h) rather than a Parhelion one. The
 * load runs on a worker thread, as the loadout module's does, and only the ready phase exposes it.
 *
 * NOT COMPILED OR TESTED.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../../state/editor/catalog.h"
#include "weapon_recipe.h"

namespace dawn::tools::parhelion::catalog {

/** Where the background load stands. Only `ready` exposes the catalog's contents. */
enum class Phase : std::uint8_t { idle, loading, ready, failed };

/** Starts the background load unless one is running or has finished. Retries after a failure. */
void start() noexcept;

/** Cancels a running load, joins its worker and drops the catalog. */
void stop() noexcept;

/** @return The current phase. */
[[nodiscard]] Phase phase() noexcept;

/** @return The loader's progress counter. Its unit is the loader's own. */
[[nodiscard]] unsigned progress() noexcept;

/** @return The failure the loader reported. Empty unless the phase is `failed`. */
[[nodiscard]] const std::string& failure() noexcept;

/** @return The loaded catalog. Empty unless the phase is `ready`, so reading it is always safe. */
[[nodiscard]] const state::editor::Catalog& items() noexcept;

/**
 * @return Indices into `items().items` for every weapon, in catalog order. Empty unless the phase
 * is `ready`.
 */
[[nodiscard]] const std::vector<std::size_t>& weapons() noexcept;

/**
 * Makes a recipe start from one stock weapon: its donor hash and name, its stat rows, its socket
 * lanes and their default perks. The author's name, text, identity hashes and any ammo or damage
 * choice are reset, which matches Parhelion's set_donor.
 * @param recipe Recipe to change.
 * @param donor Base weapon from the catalog.
 * @param catalog The catalog the donor belongs to, used to name its perks.
 */
void seed_from_donor(WeaponRecipe& recipe, const state::editor::CatalogItem& donor,
                     const state::editor::Catalog& catalog);

/**
 * Restores the base weapon's stat rows and clears any stat edits.
 * @param recipe Recipe whose stats to reset.
 * @param donor Base weapon the recipe starts from.
 */
void reset_stats(WeaponRecipe& recipe, const state::editor::CatalogItem& donor);

/**
 * Restores one socket lane to the base weapon's default perk.
 * @param recipe Recipe whose lane to reset.
 * @param donor Base weapon the recipe starts from.
 * @param catalog The catalog the donor belongs to, used to name its perk.
 * @param lane Socket lane to reset. Ignored when out of range.
 */
void reset_socket(WeaponRecipe& recipe, const state::editor::CatalogItem& donor,
                  const state::editor::Catalog& catalog, std::size_t lane);

} // namespace dawn::tools::parhelion::catalog
