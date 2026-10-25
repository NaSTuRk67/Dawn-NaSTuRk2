# Sundial item/perk definition inspector: C++ port notes

**NOT COMPILED OR TESTED.** This port was written by reading the Rust source and the Dawn headers. Nothing here has been built, linked, run, or checked against a live client. A syntax-only pass (see d) is not a build. Treat every file as unverified until it compiles against Dawn's real build and ImGui version.

## (a) Files written

All paths are under `/tmp/claude-0/-home-claude/4567336c-e7b5-53b0-a218-c39a5f69fdde/scratchpad/work/port/sundial-inspector/`.

- `Dawn/src/client/ui/inspector/inspector_panel.h` : public entry, `void draw() noexcept;`
- `Dawn/src/client/ui/inspector/inspector_panel.cpp` : Dear ImGui UI (Browse tab, Detail tab with Overview, Stats, Sockets, Perks, Progression, Technical).
- `Dawn/src/client/ui/inspector/inspector_catalog.h` : data model (`Snapshot`, `Filter`, `PlugPool`, `Acquisition`) and read API.
- `Dawn/src/client/ui/inspector/inspector_catalog.cpp` : reads Dawn build data, builds indexes and carrier lists, search and parsing.
- `Dawn/src/client/ui/inspector/inspector_stubs.h` : declarations of the name stubs.
- `Dawn/src/client/ui/inspector/inspector_stubs.cpp` : stub bodies, all returning empty.
- `NOTES.md` : this file.

No file outside `Dawn/src/client/ui/inspector/` was written. Nothing was edited in the Dawn tree.

## (b) What was ported

Source: `sundial/src/app/inspector/` (read only). The port keeps the core read-only inspector: search, browse, and a detail view with details, stats, and progression.

- **Search and browse.** The Browse tab lists every installed item definition, using a clipper. Filters: text, tier, has ordinary sockets, masterwork plug, has sandbox perks, and a Reload button. The text box accepts `0x` plus hex as an exact definition hash, or digits as a native definition index. A plain text name search returns nothing because Dawn has no name table (see c). Maps to the Rust `search.rs` and `controller.rs` flow, without the dropdown, recents, or cross-catalog hash search.
- **Overview.** Hash, native index, tier, inventory bucket (Dawn descriptor: array selector, slot range), masterwork status, Collections grant, stack limit, instanced flag, equipment slot, socket and perk and stat counts, gear art. Maps to Rust `item/overview.rs` and `item/identity.rs`.
- **Stats.** Each stat row with its investment value. Rows are labelled from the installed investment constants (light/banner power, weapon power, character stat 1 to 6) when those match, otherwise "Stat row N". Maps to Rust `overview.rs` `draw_hash_item_investment_stats`.
- **Sockets.** Socket lanes with type, initial plug (open button), and plug-choice and roll-pool counts. Each lane's choices list can be expanded and each member opened. Maps to Rust `item/sockets.rs`.
- **Perks.** Sandbox perk indices of the item, with a count of other items that carry each perk, and an expandable carrier list. Maps to Rust `runtime/perks.rs` but without the runtime action graph.
- **Progression.** Collections acquisition (granting collectible, its hash, and its material rows), and insertion and enable material-requirement sets. Maps to the cost parts of Rust `definition/progression.rs` and `materials.rs`.
- **Technical.** Raw fields: plug category hash, requirement set indices, action stat, socket entry list row, gear art, art arrangements, render overrides.

Dawn APIs used (all verified by reading the headers, not by compiling):

- `state/build_data/runtime.h`: `item_definitions_ready`, `configured_item_details_ready`, `find_configured_item_detail`, `collectible_definitions_ready`, `find_collectible_definition`, `material_requirement_sets_ready`, `find_material_requirement_set`, `socket_plug_rules_ready`, `visit_socket_plug_pool`, `visit_socket_roll_pool`, `inventory_bucket_descriptors_ready`, `find_inventory_bucket_descriptor`, `investment_constants_ready`, `find_investment_constants`.
- `state/build_data/items/item_catalog.h`: `items::count`, `items::snapshot`, `items::masterwork_plug`, `items::Tier`, `kUnavailableMaterialRequirementSetIndex`.
- `state/build_data/items/details/definition.h`: `details::Definition` and its sentinels.
- `state/build_data/collectibles/collectible_catalog.h`: `collectibles::find_granting`.
- `core/ui/components/components.h`: `filter::input`, `section::header`.
- `core/ui/scaling/dpi/ui_dpi_scaling.h`: `scaling::dpi::pixels`.

Registration pattern (not wired, see d): `client/ui/runtime/client_ui_module_runtime.cpp` registers each page with `PageRegistration::acquire(Owner::client, stableId, displayName, &draw)`. The inspector would need the same, for example `client.inspector` / `Inspector`, plus a `PageRegistration` member and a `release()` in `shutdown()`.

## (c) Stubbed or skipped

### Stubbed (clearly named, return empty; see `inspector_stubs.h`)

- `stubs::item_name_for_hash`: Dawn's item `Definition` carries no name. The Rust catalog resolves names from a manifest Dawn does not have. Effect: rows show "Unnamed definition" and the name fact reads "Not in Dawn build data". Text search therefore matches nothing.
- `stubs::perk_name`: no sandbox perk name table. Perk rows show the number and "Name not in Dawn build data".
- `stubs::stat_name`: no stat name table. Stat rows fall back to the constants labels, then to the row number.

### Not in Dawn build data (no stub, shown as absent or noted)

- Item descriptions (Rust `catalog.description`).
- Perk "active" versus "declaration only" status. Dawn's sandbox perk list has no active flag, so it is noted on the Perks tab.
- Item power and power cap, ammo type, damage type, rarity text, item class type, and item kind. Dawn exposes raw tier and masterwork fields only.
- Ability and subclass buckets (Rust `overview.rs` `draw_hash_item_abilities`). Dawn has the table (`abilities/`), but this port does not render it.
- Progression slots, objectives, unlocks, and the progression browser (Rust `progression/`, about 3,000 lines). Dawn's `progressions::Definition` holds only an index and a scope. The Progression tab says so.

### Skipped as Rust-only machinery

- On-demand package reading and its cache (`runtime/mod.rs`, `loader`, `PackageInspectionAccess`). Dawn already has the data in State, so no disk reader was ported.
- Runtime action graph decoding and `view::draw_graph` (`runtime/perks.rs`, raw finished-perk records).
- Owned-quantity requests and the account answer (`requests.rs` owned quantities). Those depend on the Rust app's publish/take mechanism. No Dawn equivalent was chosen.
- Cross-window navigation through egui temp data (`requests.rs`, `take_definition_request`, `request_definition_with_context`). The port uses a plain module-local selection instead.
- Source-context comparison of a saved instance (`instance.rs`, `draw_hash_item_source_comparison`). Dawn's inspector has no instance context to receive.
- Related Records tab and reverse lookups (`reverse.rs`, `matches.rs`, `related_catalog_records`).
- Markdown report export (`hash_inspector_report` in `controller.rs`).
- Preview and artwork (`preview.rs`, `art_arrangements`), search dropdown, recent history, and the hash-inspector floating window with its sections.
- Hash-name table use for bubble names (`hash_names/`), because Dawn's hash names are for destination bubbles, not items.
- Rust tests (`tests.rs`, `runtime/tests.rs`). None were ported.

## (d) Notes and open items

- **Syntax-only check (partial).** The three `.cpp` files passed `-fsyntax-only -std=c++20 -Wall -Wextra` under both g++ 13.3 and clang++, using Dawn's vendored `imgui.h`. To get that far the check used a scratch copy of the Dawn source with two changes that are not in this output: a stub `Windows.h` (defines `MAX_PATH`, `UINT`, `HWND`, and a few typedefs) and placeholder `spawn_sets::Point` and `spawn_sets::kPositionComponents`. No linking or runtime test was done.
- **Pre-existing gap in the Dawn snapshot.** `state/build_data/runtime.h` uses `spawn_sets::Point` and `spawn_sets::kPositionComponents`, but neither is defined anywhere in the snapshot. Any translation unit that includes `runtime.h` (this module does) fails until they exist. The real build may define them elsewhere; this was not confirmed.

- **Not wired.** The module is not registered in `client_ui_module_runtime.cpp`, and the new `.cpp` files are not added to any build list (CMake or vcxproj). Both must be done in the real tree.
- **Unverified assumptions.**
  - ImGui version. The code uses `ImGuiListClipper::Begin(count)`, `BeginTable`, `TableSetupColumn`, `BeginTabBar`, `BeginTabItem(label, nullptr, flags)`, `SmallButton`, `CollapsingHeader(label, flags)`, and `BeginChild(id, size)`. These need to match Dawn's ImGui version.
  - `Filter::operator==` is a defaulted comparison over `std::array<char, 96>`. This needs C++20 (`std::span` is already used by Dawn).
  - `fmt` (in `inspector_panel.cpp`) forwards a non-literal format to `std::snprintf`. Expect `-Wformat-nonliteral` or `-Wformat-security` warnings, depending on flags.
  - Several calls are `noexcept` but allocate (`std::vector`, `std::map`). Allocation failure is caught in `inspector_catalog.cpp` where it matters, but not in the panel's small per-frame buffers.
- **Readiness.** The module loads its snapshot once and keeps it until the user presses Reload. If it is opened before build data is ready, the page shows "not loaded yet" and Retry.
- **Name semantics.** `0x` plus up to 8 hex digits matches `definitionHash` exactly. Digits alone match the native `definitionIndex`. Tier filter uses native values 1 to 5 (Common to Exotic). Tier 0 shows as "Untiered".
- **Search cap.** Browse lists all matches with the clipper. The Rust search caps at 200. That cap is not carried over.
- **Performance.** `load()` copies the item table and one detail per item (about 15k rows in the shipped build), and builds lookup tables of 65,536 entries. Each Reload repeats that work. Plug pools are computed per frame for the open Sockets tab, which is small.
