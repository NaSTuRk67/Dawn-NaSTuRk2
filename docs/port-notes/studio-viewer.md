# Studio Viewer port: NOT COMPILED OR TESTED

Nothing here has been built, linked, or run. Treat every file as a draft until it compiles against
the Dawn tree and has been exercised against a real installed package folder.

Target: `Dawn/src/tools/studio/viewer/` in the Dawn repo. Source: the C# SunriseStudio projects
Viewport, Projects, Missions and Scene (read only). Scope: a read-only viewer of assets, maps,
mission scripts and investment data, with details for the selected item.

## (a) Files written

All paths are under `studio-viewer/Dawn/src/tools/studio/viewer/`.

| File | Role |
|---|---|
| `studio_viewer_model.h` | Row structs, the `Tab` enum, `Snapshot`, `hex32`. |
| `studio_viewer_classify.h` / `.cpp` | Category rules ported from `AssetCategories.cs` and `InvestmentItemKinds.cs`. The two class tables (57 categories, 184 investment tables) were generated from `AssetCategories.cs` by a script, not retyped. |
| `studio_viewer_stubs.h` / `.cpp` | Named `stubs::` functions for the gaps. Each returns a message or a conservative value. |
| `studio_viewer_load.h` / `.cpp` | `LoadRequest`, `LoadOutcome`, `LoadJob` (a `std::async` worker), the loader, `to_wide`, `read_preview`. |
| `studio_viewer_panel.h` / `.cpp` | `draw()` and `shutdown()`. ImGui tabs with a list and a detail pane. |

Also written: `studio-viewer/NOTES.md` (this file).

Not touched: `Dawn.vcxproj`, `Dawn.vcxproj.filters`, the module registry, and anything outside the output folder.

## (b) What was ported

Dawn APIs used (read from the headers; none invented):

- `middleware/content/packages/named_tags.h`: `named_tags::extract_directory` with a visitor, for
  every named package entry (name, tag, class, patch). This feeds Assets, Maps and the vendor names.
- `state/editor/catalog.h`: `editor::load_catalog`, `Catalog::index`, `Catalog::statNames`,
  `editor::display_stat`, and the `CatalogItem` fields (name, type, description, kind, plug, unnamed,
  inert, damageType, ammo, primaryStatRow, statGroupIndex, compatible).
- `state/build_data/items/item_catalog.h` and `items/details/definition.h`: bucket, tier, definition
  hash and index, max stack, instanced state, stats, socket types, initial plugs, sandbox perks.
- `state/build_data/vendors/vendor_catalog.h` and `definition.h`: `snapshot_index`,
  `snapshot_definitions`, `snapshot_sale_rows`, and the sale fields `itemIndex`,
  `secondaryItemIndex`, `categoryIndex`, `rowIndex`, `vendorIndex`.
- `core/ui/modules/` and `player_panel.cpp`: style only. The panel is plain ImGui, as the player
  panel is.

Per area:

- **Assets** (C# `AssetCategories`, structured classes only): one row per name and class. A name seen
  in several patches keeps its highest patch. Category is the class-based browser category.
- **Maps** (C# `MapCatalog`, root classes only): the assets whose class is the map root.
- **Missions** (C# `MissionProject.List`): `scripts/stem/stem.lua` under the project folder, sorted by
  name. Linked folders and scripts are skipped with a diagnostic. A preview of up to 64 KiB is read
  when the row is selected.
- **Items** (C# `InvestmentCatalog` and `ReadItem`): item rows in catalog order. The detail pane shows
  hash, definition index, bucket with slot name, tier, max stack, instanced, plug, damage type, ammo,
  primary stat, stats (displayed through `display_stat`, with the stored value beside it), sockets
  (type, initial plug, compatible-plug count per lane), and sandbox perks by index.
- **Vendors** (C# `InvestmentCatalog.ReadVendor`): index rows joined to named definitions and to
  their definitions for the raw reset fields. The sale list shows item, secondary item and category.
- **Classification** (C# `InvestmentItemKinds.Classify`, `SlotName`, `TierName`, `IsCurrency`): a
  faithful port of the rule order. The `Other` label for items is "Other items", as in C#.

## (c) What was stubbed or skipped

Stubs (named functions in `studio_viewer_stubs.cpp`, shown in the UI as messages):

- `stubs::map_bubbles_message`: map bubble rows need package byte reads. Named tags expose names and
  classes only.
- `stubs::asset_bytes_message`: entry type, subtype and size need the package entry table, which
  named tags omit. This also means non-structured categories (Texture, Buffer, Shader and so on) are
  not classified. Those entries are not in the list at all.
- `stubs::mission_validation_message`: the C# `MissionGraphDocument.Parse` and `RequireValid` have no
  C++ port. Scripts are listed but not checked.
- `stubs::vendor_sale_costs_message`: sale cost expressions. Dawn's vendor layout marks the cost
  fields open, so none are shown. C# `ReadVendor` cost logic is not ported.
- `stubs::item_has_objectives` returns false. The objective block is not exposed by the Dawn catalog,
  so the Quests rule runs on the type line alone. This can misfile objective items.
- `stubs::describe_class` returns the hex class ID. Dawn has no class-name table; C# `ClassNames` is
  not ported.

Skipped on purpose:

- Viewport rendering: `AssetViewport`, `NativeSceneRenderer`, `ViewerCamera`, gizmos, lights, water,
  effects, and the shader and render paths. Out of scope per the brief.
- Write paths: `ProjectBuilder`, `ModInstaller`, `ObjectEdits`, `BinaryDelta`, `ProjectTransactions`,
  `MissionProject.Save`, `Export`, and every `Edit` method. The viewer is read-only.
- Activities and scenarios: the C# `MapCatalog.ActivitiesFor` and scenario bubble links. The map
  detail says so.
- Mission graph model: `MissionGraphDocument`, `MissionScriptGraph`, `LuaScript`, `LuaText`,
  `MissionApi*`, and the visual authoring code.
- Project manifest: `ProjectManifest`, object groups, and config bindings. Dawn has no equivalent
  reader, so the viewer does not show mod changes.
- Investment vendor display labels from the localized display bank. The vendor name comes from the
  named tag instead.
- Perk names. Sandbox perks are shown by index. The C# `PerkName` and `SandboxPerkDisplays` lookups
  are not ported.
- Socket type names and the full `AllowedPlugs` pool. Dawn's `compatible` count is shown instead.
- Collections and presentation-node trees (`InvestmentCollections`).

## Assumptions to check when this is compiled

1. **Item row meaning.** `ItemRow` uses the position in `Catalog::items` as the row index. The
   catalog's `indices` map is assumed to be keyed by `definitionIndex`, since `item_name` looks plugs
   up through it.
2. **`load_catalog` finishes the catalog.** The loader assumes `editor::load_catalog` calls
   `Catalog::finish()` itself. Check this before reading `statNames`, the pools or `compatible`.
3. **Ready gates.** `load_catalog` returns false until the item catalog is ready. The loader then adds
   a diagnostic and keeps going, so Items stay empty. The vendor snapshot functions return false
   before the vendor catalog is populated. The loader then adds a diagnostic and skips vendors.
4. **Named-tag dedupe.** Keys are class plus name, and the highest patch wins. This follows the
   `named_tags` header comment, not a C# rule. The C# code may dedupe differently.
5. **Tier 0.** Shown as "None", following the C# `TierName`. The Dawn header says 0 is outside the
   ladder, so "Unknown" may be the better label.
6. **Links.** `std::filesystem::is_symlink` does not detect Windows junctions or reparse points.
   The C# `RejectLinks` checks the reparse-point attribute. A junction under `scripts/` would not be
   skipped here. This is a known gap.
7. **`u8string` type.** The `utf8()` helper reinterpret-casts `u8string()`. In C++20 that returns
   `char8_t`, and in C++17 it returns `char`. The cast compiles either way.
8. **Shutdown and the worker.** A `std::future` from `std::async` blocks in its destructor until the
   worker finishes. `shutdown()` cancels the worker first, but the wait happens when the static
   state is destroyed. Confirm this is acceptable at unload.
9. **Threading.** `draw()` runs on the frame thread. The loader runs on a `std::async` worker and
   only `poll` touches its result. The cancel and progress flags are atomics.
10. **Windows-only pieces.** `to_wide` uses `MultiByteToWideChar`. The named-tag reader takes wide
    paths. The Dawn logs module includes `Windows.h` the same way.

## Integration still needed

- Add the nine source files to `Dawn.vcxproj` and its filters. Nothing in this folder builds on its
  own.
- Call `dawn::tools::studio::viewer::draw()` from a host frame, and call `shutdown()` at unload. No
  module registry entry is added, so the viewer has no menu slot yet.
- Confirm the `tools/` directory is inside the include paths the Dawn project uses for `state/` and
  `middleware/`. The relative includes assume this layout.
