# Sundial raw JSON editor: port to Dawn (settings_json)

NOT COMPILED OR TESTED

The Dawn build (MSVC, Windows SDK, the project's own warning flags) was not run. Nothing was linked, and nothing was run inside the game. The checks below are the only ones performed.

## What was checked

- Syntax checker, built on Linux with g++ 13 (`-std=c++20 -Wall -Wextra -Wpedantic -Werror`) and run against 28 cases: valid and invalid documents, duplicate names at two depths, the 16-container nesting limit, control bytes, bad escapes, trailing content, and line/column reporting. All 28 pass. The harness is at `/tmp/claude-0/-home-claude/4567336c-e7b5-53b0-a218-c39a5f69fdde/scratchpad/syntax_harness/harness.cpp`. It is not part of this output.
- All three `.cpp` files were compiled with `-fsyntax-only -Wall -Wextra` against the real Dawn headers (`src/core`, `vendor/imgui` 1.92.6). Windows.h was replaced by a hand-written stub, so Windows API signatures were not checked against the real SDK. No errors.

## (a) Files written

Output root: `/tmp/claude-0/-home-claude/4567336c-e7b5-53b0-a218-c39a5f69fdde/scratchpad/work/port/sundial-json-editor/`

- `NOTES.md` (this file)
- `Dawn/src/client/ui/settings_json/settings_json_panel.h`: declares `draw()`.
- `Dawn/src/client/ui/settings_json/settings_json_panel.cpp`: ImGui editor window: toolbar, multiline text box, syntax and status lines, and the load, validate, save, and reload logic.
- `Dawn/src/client/ui/settings_json/settings_json_syntax.h`: `check_syntax()` and `SyntaxError` with line, column, and reason.
- `Dawn/src/client/ui/settings_json/settings_json_syntax.cpp`: strict JSON checker (new code; see the note on Dawn's parser below).
- `Dawn/src/client/ui/settings_json/settings_json_file.h`: settings.json read, write, and the two stubs.
- `Dawn/src/client/ui/settings_json/settings_json_file.cpp`: reads and atomic writes of settings.json through `core::path::artifact_file`.

No other Dawn file was written or changed. The page is not registered (see (c)).

## (b) What was ported

- **Load.** Reads `settings.json` byte for byte from the same folder the Core loader uses (`artifact_file`, file name `settings.json`). The editor shows CRLF as LF, matching Sundial's `encode_settings_for_editor`. A file over 1 MiB, or one with a NUL byte, is refused. The first draw loads the file once. Reload is a separate button.
- **Edit.** An ImGui multiline box with tab input. Each edit reruns the syntax check and reports the first error as line and column.
- **Validate.** Two steps, as in Sundial's `apply_raw_json`: (1) the strict syntax check, which follows Sundial's `strict_json` rules (rejects duplicate member names, nesting at depth 16 and beyond, control bytes in strings, and trailing content); (2) Dawn's own `dawn::core::settings::parse(text, candidate)`, the same function the Core loader uses. Step 2 reports pass or fail only, because Dawn's parser does not say which value is wrong.
- **Save.** Runs validation first. Then it re-reads the file and refuses if it differs from the bytes loaded or last saved, which corresponds to Sundial's `check_json_edit_baseline` ("the workspace changed"). It then writes through a staged sibling and `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`, so a failed write keeps the old file. It writes one `ev=settings_json stage=save` log line on every outcome.
- **Reload.** If there are unsaved edits, the first click arms a confirmation, and the second click discards the edits.
- **Unsaved indicator and status line.** Dirty state compares the editor text with the saved bytes, with CRLF normalized.

## (c) Stubbed, skipped, or not done

Stubs (clearly named, in `settings_json_file.h`):

- `stub_runtime_compatibility_check()`: always returns true. Sundial checks the runtime DLL before each write. Dawn has no equivalent. Save calls the stub, so its true result carries no meaning.
- `stub_load_bundled_defaults()`: always returns false. Sundial's "Load Defaults" reads bundled defaults for completions. Dawn's defaults are an RCDATA resource that only the Core loader reads. The button is labelled "(not ported)".

Skipped as Sundial-only or not reasonable to port in this scope:

- Folding, search and replace, pointer navigation, completions, syntax highlighting, and the fold projection.
- Custom undo history (Sundial's `Undoer`). ImGui's own text-field undo is not verified here.
- Detached window and viewport handling. Dawn draws pages through its Core UI module registry; the port is one `draw()` function.
- The account-source notice and verification, and the "newer schema" warning.
- Sundial's `settings.json.bak` backup and its future-schema upgrade handling. The port writes no backup.

Not done, outside the output folder:

- The page is not registered. To show it, add a `PageRegistration` next to the existing ones in `Dawn/src/client/ui/runtime/client_ui_module_runtime.cpp` (for example `acquire(core::ui::modules::Owner::client, "client.settings_json", "Settings JSON", &settings_json::draw)`). That file was not modified.

Dawn lacks a generic JSON parser. `Dawn/src/core/settings/parser.h` is a schema-bound reader whose helpers are private, so `settings_json_syntax.cpp` is new code, not a Dawn API. `dawn::core::settings::parse` was used as-is. Nothing was invented.

## Known differences and risks

- Duplicate member names are compared as raw bytes, so `"a"` and `"a"` are not treated as duplicates. Sundial compares decoded names.
- Bytes of 0x80 and above inside strings are not checked as UTF-8. Sundial's `&str` input guarantees valid UTF-8, and the Dawn loader does not check it either. Invalid UTF-8 could reach `core::settings::parse`.
- An object with more than 1024 members is refused with an explicit message, because duplicate checking uses fixed storage.
- Saved text is the editor text as typed, with LF line endings and no re-encoding. Sundial re-encodes. Unknown fields survive because nothing is removed.
- Core settings are read once at startup (per the comment in `Dawn/src/client/player/player_settings_store.cpp`). A save does not change a running game until it restarts. The status line says so.
- Memory is static: three 1 MiB text buffers (editor, saved bytes, scratch) plus about 256 KiB for the duplicate-name tables. The checker is not thread-safe. Call it from the UI thread only.
- Each edit reruns the syntax check on the whole text, and each frame compares the text with the saved bytes. Cost is O(file size). Not measured.
- The Tab key is captured by the text box (`ImGuiInputTextFlags_AllowTabInput`). Keyboard focus cannot be moved away with Tab.
- A crash during save can leave `settings.json.editor.tmp` beside the settings file. The next save overwrites it.
- An older-version file is shown as stored. Dawn's loader upgrades it on the next startup. The editor does not upgrade it.
