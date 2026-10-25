/**
 * Build and save for a Parhelion weapon recipe. The save writes the recipe document through the
 * Dawn artifact helper, which creates the folder and replaces the file whole.
 *
 * NOT COMPILED OR TESTED.
 */

#include "weapon_build.h"

#include <string>

#include "../../core/filesystem/path.h"
#include "../../core/logging/log.h"

namespace dawn::tools::parhelion {
namespace {

/** Folder under the Dawn artifact directory that holds saved recipes. */
constexpr std::string_view kRecipeFolder = "parhelion\\recipes\\";
constexpr std::string_view kRecipeSuffix = ".parhelion.json";

} // namespace

std::string recipe_file_stem(const WeaponRecipe& recipe) {
    return slug_from_text(text_view(recipe.name));
}

bool check_recipe(const WeaponRecipe& recipe, std::string& json, std::string& error) {
    return to_json(recipe, json, error);
}

bool build_package_stub(const WeaponRecipe& recipe, std::string& error) {
    std::string json;
    if (!check_recipe(recipe, json, error)) {
        return false;
    }
    // STUB: Parhelion's package compiler (weapon/build.rs, tables, emission) has no Dawn port.
    error = "Packaging is not ported to Dawn yet. The recipe checks out and can be saved, but no "
            "game package was written.";
    core::log::write(core::log::Channel::client, core::log::Level::warn,
                     "ev=parhelion stage=package result=stub");
    return false;
}

bool save_recipe(const WeaponRecipe& recipe, std::string& savedPath, std::string& error) {
    std::string json;
    if (!check_recipe(recipe, json, error)) {
        core::log::write(core::log::Channel::client, core::log::Level::warn,
                         "ev=parhelion stage=save result=invalid");
        return false;
    }

    const std::string stem = recipe_file_stem(recipe);
    // The stem is ASCII letters, digits and hyphens, so each byte widens without a conversion.
    std::wstring relative(kRecipeFolder.begin(), kRecipeFolder.end());
    for (const char character : stem) {
        relative.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
    }
    relative.append(kRecipeSuffix.begin(), kRecipeSuffix.end());

    if (!core::path::write_artifact_text(relative, json)) {
        error = "Could not write the recipe file in the Dawn artifact folder.";
        core::log::write(core::log::Channel::client, core::log::Level::error,
                         "ev=parhelion stage=save result=fail");
        return false;
    }
    savedPath = std::string(kRecipeFolder) + stem + std::string(kRecipeSuffix);
    core::log::write(core::log::Channel::client, core::log::Level::info,
                     "ev=parhelion stage=save result=ok");
    return true;
}

} // namespace dawn::tools::parhelion
