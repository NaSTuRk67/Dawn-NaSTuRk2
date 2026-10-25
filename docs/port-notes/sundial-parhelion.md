# Parhelion weapon workflow: port notes

**NOT COMPILED OR TESTED.** Nothing here has been built, linked, loaded into Dawn, or run. The
only check was a host syntax-only pass (g++ 13.3, `-fsyntax-only -Wall -Wextra`) of three files,
`weapon_recipe.cpp`, `weapon_build.cpp` and `weapon_catalog.cpp`, against Dawn's real headers. It was
clean for this code. The panel could not be checked, because Dawn's `ui_dpi_scaling.h` includes
`Windows.h` and this Linux host has no Windows toolchain. Read the code as unverified.

Source: `sundial/crates/parhelion/` (read-only). Target: `Dawn/src/tools/parhelion/`, mirrored under
this folder as `Dawn/src/tools/parhelion/`, as the brief asked. `Dawn/src/` has no `tools/` folder
before this port, so the port creates it. The Dawn checkout also has a repo-root `tools/` beside
`Dawn/` (holding `build`, `install`, `re`, `coo` and others). Placement is a question to settle:
the new module may belong at `dawn/tools/parhelion/` instead. Its relative includes assume the
`Dawn/src/tools/parhelion/` depth, so moving it means adjusting those paths.

## (a) Files written

All under `/tmp/claude-0/-home-claude/4567336c-e7b5-53b0-a218-c39a5f69fdde/scratchpad/work/port/sundial-parhelion/`:

- `Dawn/src/tools/parhelion/weapon_recipe.h`: recipe model (`WeaponRecipe`, stat and socket
  overrides), fixed-buffer helpers, and the declarations for the recipe rules.
- `Dawn/src/tools/parhelion/weapon_recipe.cpp`: namespace and slug derivation, canonical hash
  parsing, `validate`, and `to_json` (the recipe writer).
- `Dawn/src/tools/parhelion/weapon_build.h`: build step, package stub, and save step declarations.
- `Dawn/src/tools/parhelion/weapon_build.cpp`: `check_recipe`, `build_package_stub`, `save_recipe`.
- `Dawn/src/tools/parhelion/weapon_catalog.h`: catalog phase API and recipe seeding from a donor.
- `Dawn/src/tools/parhelion/weapon_catalog.cpp`: background load on a worker thread, and
  `seed_from_donor`, `reset_stats`, `reset_socket`.
- `Dawn/src/tools/parhelion/parhelion_panel.h`: `draw()` and `shutdown()`.
- `Dawn/src/tools/parhelion/parhelion_panel.cpp`: ImGui page for the weapon editor.
- `NOTES.md`: this file.

## (b) What was ported

Scope: the core weapon-creation workflow. The recipe is the Rust `WeaponRecipe`, reduced to:

- **Identity**: name, flavor, source. Namespace derived from the name with the same rules as
  `namespace_for_weapon_name` (ASCII slug, `parhelion.` prefix, 54-character slug limit, Windows
  reserved-name prefix).
- **Base weapon**: a donor picked from Dawn's own item catalog (`state::editor::Catalog`), loaded
  on a worker thread the same way the loadout module does it. Choosing a donor resets stats and
  perks to the donor's values, as Rust's `set_donor` does.
- **Stats**: rows of stat row and raw value, with add, remove and reset. Removed donor rows are
  recorded in `removed_investment_stats`, as the Rust format does.
- **Perks**: one entry per donor socket lane. Choices are the donor's own pool for that lane
  (`PlugScope::compatible`). Unedited lanes are written as `null`, which keeps the donor's perks.
- **Ammo and damage**: optional overrides, written only when set.
- **Validation**: name present and valid, base weapon chosen, identity hashes canonical, no duplicate
  or conflicting stat rows, edited sockets holding at least one perk.
- **Recipe file**: JSON in the shape of Parhelion's `WeaponRecipe` schema 1. Field names, the
  `sunrise_badge` placement, hash strings as `"0x%08X"`, and the `overrides` keys follow the Rust
  serde attributes. The writer is hand-written, not serde-compatible byte for byte.
- **Save**: writes `parhelion\recipes\<slug>.parhelion.json` under Dawn's artifact folder
  (`core::path::write_artifact_text`), replacing any file with the same slug.
- **UI**: sections for Identity, Base Weapon, Weapon Stats, Perks and Sockets, Ammo and Damage, and
  the actions Check Recipe, Build & Stage and Save Recipe. Style follows `player_panel.cpp` and
  `camera_panel.cpp`: `ImGui::` calls, `picker::control`, `scaling::dpi::pixels`, `(void)` on ignored
  results, fixed `std::array` text buffers.

## (c) What was stubbed or skipped

### Stubbed (clearly named in code)

- **Package build.** `build_package_stub` runs the check and then always returns false, with the
  message "Packaging is not ported to Dawn yet". Parhelion's package compiler (`weapon/build.rs`,
  `tiger-pkg`, `sundial::package_authoring`, emission, staging and manifest) has no Dawn equivalent.
  So nothing is installed, and Build & Stage never produces a package.
- **Identity hashes.** Parhelion derives item, collectible, unlock, name, flavor and source hashes
  from the namespace with `WeaponCloneIdentity::from_namespace`. That is not ported. The author types
  the six hashes, and validation only checks that they are canonical. Renaming does not regenerate
  them, which differs from Rust's `rename_authored_item`.
- **Socket choice cap.** `kMaxChoices = 8` is a placeholder. Parhelion's
  `MAX_AUTHORED_EMBEDDED_SOCKET_CHOICES` was not read.

### Skipped (outside the core workflow, or Rust-only)

- Custom perk workbench, custom perk programs, behaviors (`weapon_behavior.rs`), engine catalog.
- Shader, donor and appearance views, dyes, icons, ornaments, HUD icon, lore, locale text, badges,
  art arrangements, render gear and icon donors, presentation donors.
- Collections placement and presentation (only `sunrise_badge` is written).
- Rarity, power cap groups, stat groups and stat display curves, weapon pattern, variable damage,
  unique weapon behavior, raw payload patches, runtime values, runtime resource patches.
- Subclass, armor and other item kinds (the recipe is weapon-only, and `kind` is omitted).
- Recipe library, load, restore and backup, duplicate, rebase, staging retention, install and
  uninstall, and the Rust app shell.
- Parhelion's deeper `to_spec` checks against the donor and catalog. A saved recipe can still be
  rejected by Parhelion's own loader.
- Experimental-features gating. The Rust editor is behind a preference, and this port has no gate.

## Assumptions to verify

- **Stat rows.** Dawn's `details::Stat::row` (a `uint8` stat table row) is assumed to equal Rust's
  investment-stat `definition_index` (a `uint16`). The stat display curves are not applied, so raw
  values may not match the game's shown values.
- **Socket lanes.** Dawn's `ordinarySocketCount` is assumed to be the donor's socket count, and
  `initialPlugIndices[lane]` the default perk. Rust's `socket_columns` must be "complete positional
  columns", so an edited file must match what Parhelion expects for that donor.
- **Plug hashes.** `CatalogItem::definition.definitionHash` is assumed to equal Rust's item hash for
  the same definition.
- **Damage types.** Dawn's `DamageType::none` is taken to be Rust's `kinetic`. The port maps the
  override to the names Parhelion reads.
- **Name buffer.** The 64-byte name buffer is a UI size, not a Rust limit.

## Other behavior differences

- Save replaces a same-slug file. Rust stages each recipe under a unique name.
- Saved recipes go under Dawn's artifact folder, not a Parhelion library, and nothing reads them back.
- `draw()` and `shutdown()` are `noexcept`, as Dawn's panels are. Allocation failure inside the
  `std::string` work would terminate the process.
- The picker label lists are built once, when the catalog is ready, and cleared on `shutdown()`.
  Labels borrow catalog strings, so they must not outlive the catalog. `catalog::stop()` runs only
  from `shutdown()`.

## Not wired in (outside `tools/parhelion`)

To make the module reachable, the following are needed. None of them were written, because they sit
outside the tree this port was asked to produce:

1. Register a page. In `Dawn/src/client/ui/runtime/client_ui_module_runtime.cpp`, add a
   `core::ui::modules::registry::PageRegistration` with `Owner::client`, a stable ID such as
   `"client.parhelion"`, a display name, and `&tools::parhelion::draw`. Release it in `shutdown()`
   and call `tools::parhelion::shutdown()` alongside it.
2. Add the new `.cpp` files to the build (`Dawn/Dawn.vcxproj`, or the CMake equivalent).
3. Confirm the `dawn::tools` namespace is acceptable beside `dawn::client`, `dawn::core`,
   `dawn::state` and `dawn::server`.

## Source notes

- The task brief said about 30,000 lines. The Parhelion crate is larger: its top-level `src/*.rs`
  files total 29,194 lines, and all `.rs` files under the crate total 175,521 lines, including
  tests and submodules. Only the recipe, workflow, editor and weapon-build paths were read in
  detail.
- Dawn has a stale flat copy `Dawn/src/client/ui/player_panel.cpp` next to the live
  `Dawn/src/client/ui/player/player_panel.cpp`. The registration uses the `player/` copy, and this
  port follows that one.
