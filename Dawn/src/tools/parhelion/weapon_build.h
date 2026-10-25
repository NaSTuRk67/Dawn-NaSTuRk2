#pragma once

/**
 * The build and save steps for one Parhelion weapon recipe.
 *
 * Checking and saving are real: the recipe is validated, written as the Parhelion recipe document,
 * and stored under the Dawn-owned artifact folder. Packaging is a stub, because Dawn has no
 * equivalent of Parhelion's package compiler.
 *
 * NOT COMPILED OR TESTED.
 */

#include <string>

#include "weapon_recipe.h"

namespace dawn::tools::parhelion {

/**
 * Build step, dry run: validates the recipe and produces the document a save would write.
 * @param recipe Recipe to check.
 * @param json Receives the recipe document when the check passes.
 * @param error Receives the first problem found.
 * @return True when the recipe is valid.
 */
[[nodiscard]] bool check_recipe(const WeaponRecipe& recipe, std::string& json, std::string& error);

/**
 * STUB. Build & Stage in Parhelion compiles the recipe into game packages. Dawn has no such
 * compiler, so this only runs the check and always reports that packaging is not ported.
 * @param recipe Recipe to build.
 * @param error Receives the reason the build did not produce a package.
 * @return Always false.
 */
[[nodiscard]] bool build_package_stub(const WeaponRecipe& recipe, std::string& error);

/**
 * Save step. Checks the recipe, then writes <slug>.parhelion.json under the Dawn artifact folder,
 * replacing any file of the same name. It does not write a game package.
 * @param recipe Recipe to save.
 * @param savedPath Receives the path relative to the artifact folder when the save succeeds.
 * @param error Receives the reason the save failed.
 * @return True when the file was written.
 */
[[nodiscard]] bool save_recipe(const WeaponRecipe& recipe, std::string& savedPath, std::string& error);

/** @return The file stem a recipe saves under: its slug, which the name determines. */
[[nodiscard]] std::string recipe_file_stem(const WeaponRecipe& recipe);

} // namespace dawn::tools::parhelion
