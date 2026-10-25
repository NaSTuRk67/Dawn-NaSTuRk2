# Mission graph port: C# Sunrise Studio to Dawn (Dear ImGui)

**NOT COMPILED OR TESTED.** These files were written by reading and writing only. No compiler, no build, and no run were done. The code has not been checked against Dawn's build, the vendored ImGui 1.92.6 headers were checked only by grep, and nothing here is claimed to work.

## (a) Files written

All paths are under `studio-mission-graph/`, mirroring the Dawn tree at `Dawn/src/tools/studio/mission_graph/`. The directory does not exist in Dawn today.

1. `Dawn/src/tools/studio/mission_graph/lua_scan.h`: token and top-level statement types, and the lexer and splitter declarations.
2. `Dawn/src/tools/studio/mission_graph/lua_scan.cpp`: hand-written Lua 5.4 lexer and top-level statement splitter. It never runs Lua.
3. `Dawn/src/tools/studio/mission_graph/script_graph.h`: `Node`, `Edge`, and `ScriptGraph` (parse, lookup, and text edits that return new source).
4. `Dawn/src/tools/studio/mission_graph/script_graph.cpp`: statement classification, definition-to-use linking, and the edit operations.
5. `Dawn/src/tools/studio/mission_graph/mission_graph_canvas.h`: `Canvas`, `CanvasNode`, `CanvasEdge`, `CanvasRequest`.
6. `Dawn/src/tools/studio/mission_graph/mission_graph_canvas.cpp`: ImGui node canvas (pan, zoom, drag, link drag, delete).
7. `Dawn/src/tools/studio/mission_graph/mission_graph_panel.h`: public entry points `open_file()` and `draw()`.
8. `Dawn/src/tools/studio/mission_graph/mission_graph_panel.cpp`: toolbar, add row, canvas plus inspector, undo and redo, file read.
9. `Dawn/src/tools/studio/mission_graph/mission_graph_stubs.h`: two named stubs (see section c).
10. `NOTES.md`: this file.

Not written: no `Dawn.vcxproj` entries and no changes to any file in the real Dawn tree. The new `.cpp` files must be added to the project by hand.

Read as the source: `SunriseStudio.Missions/LuaScript.cs`, `MissionScriptGraph.cs`, `SunriseStudio.Editor/NodeCanvas.cs` (lines 1 to 330), `MissionGraphPage.cs`, and `MissionGraphPage.Nodes.cs`. Other C# files named in section (c) were identified by name only and not read.

Dawn conventions followed: `#pragma once`, `dawn::...` namespaces, `/** */` doc comments, snake_case functions, camelCase fields, `[[nodiscard]]` and `noexcept` on the public surface, `(void)` on discarded results, and `ImGui::` calls without `ImGuiNET`. Taken from `client/ui/player/player_panel.cpp` and `state/activity/coo/mission_script.h`. C++20 is assumed, matching `Dawn.vcxproj` (`stdcpp20`).

## (b) What was ported

- **Graph of script nodes.** Each top-level Lua statement is a node. The kinds are module (`local x = require ...`), function (`function f()`, `local function f()`, `f = function`), value (`local a, b = ...`, `a = ...`), and code (everything else, including `if`, loops, `return`, and calls). Titles, subtitles, and the code preview follow the C# shape.
- **Links.** A name that a statement defines links to each later statement that uses it. Member names after `.` or `:` and table keys do not count as uses. The last definition wins, and a declaration without a value does not replace an earlier one. This mirrors `MissionScriptGraph.Link`.
- **Read the Lua into the graph.** `open_file()` accepts `.lua` files only, up to 64 MiB, the same limit as the C# reader. It handles a UTF-8 BOM, `--` and `--[[ ]]` comments, `"` and `'` strings, long strings, numbers, and operators. It splits statements by tracking `function`, `if`, `do`, and `repeat` against `end` and `until`, plus bracket depth, with a new statement starting on a later line after a complete expression. Comment lines directly above a statement belong to it.
- **Add.** `with_statement()` inserts before a final `return`, or appends. It is an "Add statement" box above the canvas.
- **Move.** Two forms. Dragging a node changes its canvas position only, which is the C# `Positions` behaviour. "Move up" and "Move down" swap the statement's text with its neighbour, as `ScriptGraph.Move` does.
- **Connect.** Drag from a node's right dot to another node's left dot (or onto its body) to create a link. See section c, because this differs from the C# view.
- **Delete.** The Delete key on a selected node removes its statement, or removes a selected editable link. The inspector's "Delete" button does the same for a node.
- **Edit code.** The inspector edits a node's code, including its comment lines, and writes it back with "Apply". "Revert" reloads it.
- **Undo and redo.** Each edit stores the previous source text, up to 200 steps. Every edit is re-parsed before it is accepted, so the script never ends up unreadable.
- **Pan and zoom.** Middle-drag or left-drag on empty space pans. The wheel zooms around the pointer. "Frame all" fits the graph. "Arrange" lays the nodes out in rows, in script order.
- **Status line.** Errors from the tokenizer or splitter, and the outcome of each edit, appear in colour.

## (c) What was stubbed or skipped

**Stubs** (`mission_graph_stubs.h`), each named and commented as a stub:
- `stubs::save_layout(path)`: C# `MissionLayoutStore` saves node positions beside the script. Dawn has no equivalent, so this returns false and writes nothing. It is called when a node drag ends, through `Canvas::take_moved()`. Positions last only for the session.
- `stubs::typed_api_checks_available()`: C# checks each typed API call against the mission SDK schema (`MissionApiSchema`, `MissionApiResolver`, `MissionApiWriter`, and related files). Not ported, so this returns false, and the panel says "API calls are not checked".

**Skipped, with the reason:**
- **Lua execution.** Skipped as instructed. Dawn's `state/activity/coo/script_lua.cpp` and `mission_script.cpp` evaluate Lua through the Lua VM, so they were not used.
- **Loretta parser.** The C# reader uses Loretta, a full Lua parser. This port uses a much smaller lexer and splitter. Expect differences on unusual syntax, such as `1..2` lexed as numbers, `local x <const>` attributes, and a `(` starting a line, which is read as a continuation as Lua does.
- **Returned callback tables.** C# splits `return { on_x = function ... }` into one node per field, with callback kinds and API event lookups. Here the whole `return` is one code node, so callbacks are not separate nodes.
- **Flow and campaign groups.** `MissionGraphDocument`, `MissionVisualAuthoring`, flow groups, and the "Flow" and "Campaign" group picker are not ported.
- **Lua text tab.** The C# "Lua" tab edits the whole source. Only per-node editing is here.
- **Saving and catalogs.** No save to disk, no export, no installed-scripts catalog, no mod project list, and no SDK reference reading. `open_file()` reads one file, and the caller can read `ScriptGraph::source()` to save.
- **Scene checks and diagnostics.** The C# "Checks" tab (`MissionGraphValidation`, `SceneDiagnostics`) is not ported.
- **Context menus, shortcuts, and the Disconnect-by-drag gesture.** Only Delete and the buttons are supported.

**Deliberate differences:**
- **Connect is view-only.** The C# script view has no connect: its links are computed from names and are read-only (`Editable = false`), and only flow steps can be connected. The request was for connect, so drawing a link adds a view-only link labelled "linked for viewing". It is never written to the Lua, and it is lost on reload. The panel says so when the link is made.
- **Delete removes the comment lines above a node.** The C# `RemoveSpan` for statements leaves those comments behind. Here they go too, because they describe the node.
- **Keys shift after edits.** A key is the kind plus name, or the keyword plus an ordinal, so a code node's key can change when an earlier one is removed. Its canvas position then resets to the next free grid cell. A selection can also move to a different node. Layouts are per session only.
- **Moves do not check order.** Swapping statements can put a use above its definition. The C# `Move` does not check this either, but the note is here because nothing else catches it.

## Open items to check when this is built

- Add all ten files to the real project. Confirm `imgui.h` (1.92.6) accepts each call, in particular `InputTextMultiline` with the `capacity() + 1` buffer, which copies the `imgui_stdlib` pattern and has not been run.
- Confirm `std::erase_if`, `starts_with`, `ends_with`, and `contains` compile under Dawn's toolchain.
- The Lua reader needs real mission scripts run through it. Compare the node count and links against the C# view for the same file.
