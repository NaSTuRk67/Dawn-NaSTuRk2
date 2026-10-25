# Sundial randomizer and armor stat adjuster: port notes

NOT COMPILED OR TESTED. Nothing in this folder has been built, linked, run, or checked against the game.

Source: `/tmp/claude-0/-home-claude/4567336c-e7b5-53b0-a218-c39a5f69fdde/scratchpad/work/sundial/src/app/equipment/` (Rust, egui).
Target: `Dawn/src/client/ui/randomizer/` in this folder (C++, Dear ImGui).

## (a) Files written

- `Dawn/src/client/ui/randomizer/randomizer_model.h`: constants, `LoadoutOptions`, `Feedback`, `Outcome`, `ArmorPreview`, `PanelState`.
- `Dawn/src/client/ui/randomizer/randomizer_bridge.h`: the stubbed seam between the panel and the account editor.
- `Dawn/src/client/ui/randomizer/randomizer_bridge.cpp`: stub bodies. None of them reads or changes an account.
- `Dawn/src/client/ui/randomizer/randomizer_panel.h`: `dawn::client::ui::randomizer::draw() noexcept`.
- `Dawn/src/client/ui/randomizer/randomizer_panel.cpp`: the two sections, drawn with ImGui.
- `NOTES.md`: this file.

Not edited: `client_ui_module_runtime.cpp`, `Dawn.vcxproj`, or anything outside this folder. The panel is not registered and not in the build.

## (b) What was ported

Rust `app/equipment/randomize.rs` (menu and dialog) and `randomize/loadout_dialog.rs` to the client panel:
- Loadout section: Weapons, Armor, Equipment / Flair, Subclass checkboxes (Rust defaults: all off).
- Replace Existing Inventory Items (off) and Keep Locked Items (on).
- Randomize Loadout button, disabled until one section is checked.
- Rust tooltip and help text, in the same words.

Rust `app/equipment/armor_stats_adjuster.rs` and `armor_stats_adjuster/ui.rs` to the client panel:
- Six goal sliders, 0 to 100, in `kStatNames` order: Mobility, Resilience, Recovery, Discipline, Intellect, Strength. Each has a tooltip in the Rust wording.
- Per-stat Current and Projected readouts. Projected turns amber where a goal is short.
- "Use Better Armor from Character Inventory" checkbox (on by default).
- Clear and Adjust Armor buttons. Adjust is disabled unless the preview would change something.
- Status line with the Rust states: "Set one or more goals", "Targets met", "Closest match - N goals short".
- Armor preview summary line: "N pieces - N swaps - N masterworks - N plugs", or "No armor changes".

Rust `randomize/rng.rs` is not ported. The Rust xorshift RNG is replaced in Dawn's own editor by `std::mt19937` (the editor's `edit::randomize` takes one), so nothing new was needed.

## Overlap with existing Dawn code (decide before wiring this in)

Dawn already implements this feature in other layers:
- `src/state/editor/edit.h` and `armor_plan.h`: `edit::randomize`, `edit::armor_input`, `edit::plan_armor`, `edit::apply_armor_plan`, `edit::adjust_stats`, `edit::adjustable_stats`, `edit::kStatCap`.
- `src/core/ui/modules/loadout/pages/equipment.cpp`: the Randomize Loadout modal.
- `src/core/ui/modules/loadout/pages/optimizer.cpp`: the Armor Stats sheet and Adjust Armor.

Those layers hold the account draft and the catalog. The client panel does not. This port therefore draws the UI only and stops at the bridge. Wiring this panel in would duplicate the existing Core loadout module. The likely intended use is to keep the existing Core module and drop this port, or to connect this panel to the editor session through the bridge.

## (c) Stubbed or skipped

Stubbed (in `randomizer_bridge.cpp`, all report "not connected"):
- `bridge::is_connected()`: always false, so the panel disables both sections.
- `bridge::randomize_loadout(options, itemPower)`: no editor session to call `edit::randomize` with.
- `bridge::equipped_totals()`: returns zero totals. Declared, not called by the panel.
- `bridge::preview_armor(targets, allowInventorySwaps)`: returns an unavailable preview.
- `bridge::adjust_armor(targets, allowInventorySwaps)`: changes nothing.

Each stub comment names the editor function a real implementation would call. Those names come from `state/editor/edit.h` and `armor_plan.h`, but the call shapes are not verified against the editor's current signatures.

Carried in the options but not applied by a real session, because the editor function has no matching parameter:
- Replace Existing Inventory Items.
- Keep Locked Items.

Skipped:
- Rust "Random Item Builder" (`item_builder.rs`, about 1000 lines). Dawn's Core loadout module has its own item path.
- Rust plug safety modes (`PlugSelectionMode`, the Perk Safety group and the Really Unsafe confirmation). Dawn has no equivalent in the client layer.
- Per-piece armor preview table (`draw_preview`, `preview_column_widths`, `draw_piece_stat_breakdown`). The panel shows a summary line only. The data for the table is in Dawn's `ArmorPlan::pieces`, which the client cannot reach.
- Background solver and debounce (`PREVIEW_DEBOUNCE_SECONDS`, the worker thread). The panel recomputes the preview synchronously on each change through the bridge. That is fine for a stub and needs revisiting for a real solver.
- Rust `app/equipment/randomize/tests*` and `armor_stats_adjuster/tests.rs` and `armor_stat_allocation` solver code (`solver.rs`, about 700 lines). Dawn has the solver in `state/editor/armor_plan.cpp`.
- Icon textures (`catalog.armor_stat_icon_texture`, `catalog.icon_texture`). No client equivalent was checked.
- Dialog sizing helpers (`dialog_size_constraints`) and the Rust window and multi-window layout. Replaced by a single panel inside the Core UI frame.

Added, not in the Rust source:
- "New Item Power" field. Dawn's Core randomizer has one and `edit::randomize` needs a level. The Rust randomizer has no equivalent. The bridge should convert Power to a level the way `level_of()` does in `core/ui/modules/loadout/internal.h`. The panel does not do that conversion itself.

## Style and conventions followed

- `dawn::client::ui::<module>` namespace, a header with one `draw() noexcept`, an anonymous namespace in the `.cpp`, `(void)` on ignored ImGui results, `ImGui::BeginDisabled`/`EndDisabled` (used elsewhere in the Client UI), and `ImGui::SetTooltip` on hover.
- `#include` paths are relative to the module, like `player_panel.cpp`.
- The file-level "NOT COMPILED OR TESTED" line is in each new file.

## Open items before any build

- Confirm the ImGui version Dawn builds against supports `BeginDisabled`, `ImGuiHoveredFlags_AllowWhenDisabled`, and `DragInt` with `ImGuiSliderFlags_AlwaysClamp`. Other Client panels use `BeginDisabled`, and the Core loadout uses `DragInt` with the same flag.
- Add the `.cpp` files to `Dawn.vcxproj` if the module is kept.
- Register `randomizer::draw` in `client_ui_module_runtime.cpp` if the module is kept. Note that the Core loadout module may already own the same feature.
- Replace the stub bridge with a real session backend before expecting any behavior.
