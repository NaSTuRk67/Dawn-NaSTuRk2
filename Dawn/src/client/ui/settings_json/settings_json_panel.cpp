/**
 * Raw settings.json editor: load, edit, validate, save. Save writes the file, not the running game.
 * Core settings are read once at startup, so changes apply after the next restart.
 */

#include "settings_json_panel.h"

#include <imgui.h>

#include <array>
#include <cfloat>
#include <cstddef>
#include <cstdio>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../../core/settings/settings.h"
#include "settings_json_file.h"
#include "settings_json_syntax.h"

namespace dawn::client::ui::settings_json {
namespace {

/** Height of the text box. The panel scrolls rather than growing past it. */
constexpr float kEditorHeight = 360.0F;
/** Status and syntax line buffers. Sized for one line of plain wording. */
constexpr std::size_t kLineCapacity = 256;

/** Editor state. Static, so the 1 MiB buffers stay out of the call stack. */
struct Editor {
    /** Editor text. Always null-terminated and free of NUL bytes. */
    std::array<char, kDocumentCapacity + 1> text{};
    /** Bytes of settings.json as last read or written. CRLF is kept here. */
    std::array<char, kDocumentCapacity + 1> disk{};
    std::size_t diskLength{};
    /** Scratch for re-reading the file before a write, to detect outside changes. */
    std::array<char, kDocumentCapacity + 1> current{};
    bool initialized{};
    bool discardArmed{};
    bool syntaxValid{};
    SyntaxError syntax{};
    std::array<char, kLineCapacity> status{};
};

[[nodiscard]] Editor& editor() noexcept {
    static Editor instance;
    return instance;
}

/** @param message Fixed wording. Truncated to the status buffer if it is too long. */
void set_status(Editor& e, const char* message) noexcept {
    (void)std::snprintf(e.status.data(), e.status.size(), "%s", message);
}

/** Writes one structured log line for a save, in the ev=... style the Core loader uses. */
void log_save(bool ok, const char* reason) noexcept {
    std::array<char, 128> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=settings_json stage=save result=%s reason=%s",
                                      ok ? "ok" : "fail",
                                      reason);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         ok ? core::log::Level::info : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * @return True when the editor text equals the disk bytes once CRLF becomes LF. The editor shows LF,
 * so this is the only fair comparison for "has unsaved edits".
 */
[[nodiscard]] bool matches_disk(std::string_view disk, std::string_view text) noexcept {
    std::size_t diskIndex = 0;
    std::size_t textIndex = 0;
    while (diskIndex < disk.size()) {
        char current = disk[diskIndex];
        if (current == '\r' && diskIndex + 1 < disk.size() && disk[diskIndex + 1] == '\n') {
            current = '\n';
            ++diskIndex;
        }
        if (textIndex >= text.size() || text[textIndex] != current) {
            return false;
        }
        ++diskIndex;
        ++textIndex;
    }
    return textIndex == text.size();
}

[[nodiscard]] bool has_unsaved_edits(const Editor& e) noexcept {
    return !matches_disk(std::string_view(e.disk.data(), e.diskLength),
                         std::string_view(e.text.data()));
}

/** Runs the strict syntax check on the current text. Cheap enough to run on every edit. */
void check_syntax_now(Editor& e) noexcept {
    e.syntaxValid = check_syntax(std::string_view(e.text.data()), e.syntax);
}

/**
 * @return True when Dawn's own settings parser accepts the text. It reports only pass or fail, and
 * the fields it checks are the ones the Core loader reads.
 */
[[nodiscard]] bool matches_supported_settings(std::string_view text) noexcept {
    static core::settings::Settings candidate{};
    candidate = core::settings::defaults();
    return core::settings::parse(text, candidate);
}

/** Reads settings.json into the editor, replacing the text. Refuses NUL bytes. */
bool load_from_disk(Editor& e) noexcept {
    // Read into scratch first. The baseline in e.disk changes only when the whole read succeeds.
    std::size_t length = 0;
    if (!read_settings_file(e.current, length)) {
        set_status(e, "Could not read settings.json. The editor text was not changed.");
        return false;
    }
    const std::string_view raw(e.current.data(), length);
    if (raw.find('\0') != std::string_view::npos) {
        set_status(e, "settings.json contains a NUL byte, so it cannot be edited here.");
        return false;
    }
    for (std::size_t index = 0; index < length; ++index) {
        e.disk[index] = raw[index];
    }
    e.diskLength = length;
    std::size_t out = 0;
    for (std::size_t in = 0; in < length; ++in) {
        if (raw[in] == '\r' && in + 1 < length && raw[in + 1] == '\n') {
            continue;
        }
        e.text[out++] = raw[in];
    }
    e.text[out] = '\0';
    e.discardArmed = false;
    check_syntax_now(e);
    return true;
}

/** Formats the last syntax error as one line: location, then reason. */
void format_syntax_error(const Editor& e, std::array<char, kLineCapacity>& line) noexcept {
    (void)std::snprintf(line.data(),
                        line.size(),
                        "Syntax error at line %zu, column %zu: %s",
                        e.syntax.line,
                        e.syntax.column,
                        e.syntax.message.data());
}

/** Full validation: strict syntax, then Dawn's supported-settings check. Sets the status line. */
bool validate_full(Editor& e) noexcept {
    check_syntax_now(e);
    if (!e.syntaxValid) {
        format_syntax_error(e, e.status);
        return false;
    }
    if (!matches_supported_settings(std::string_view(e.text.data()))) {
        set_status(e,
                   "The JSON is valid, but Dawn rejects these settings. "
                   "Dawn does not say which value failed.");
        return false;
    }
    set_status(e, "JSON is valid and matches the supported settings.");
    return true;
}

/**
 * Saves the editor text to settings.json. Refuses when the text is invalid or the file changed on
 * disk since it was loaded, so a save never overwrites an outside edit silently.
 */
void save(Editor& e) noexcept {
    if (!validate_full(e)) {
        log_save(false, "validation");
        return;
    }
    if (!stub_runtime_compatibility_check()) {
        set_status(e, "Runtime compatibility check failed. Nothing was written.");
        log_save(false, "compatibility");
        return;
    }
    std::size_t currentLength = 0;
    if (!read_settings_file(e.current, currentLength)) {
        set_status(e, "Save refused: settings.json could not be re-read. Nothing was written.");
        log_save(false, "reread");
        return;
    }
    const bool changedOutside =
        currentLength != e.diskLength
        || std::string_view(e.current.data(), currentLength)
               != std::string_view(e.disk.data(), e.diskLength);
    if (changedOutside) {
        set_status(e,
                   "settings.json changed on disk after it was loaded. Reload from disk, then "
                   "reapply your changes. Nothing was written.");
        log_save(false, "conflict");
        return;
    }
    const std::string_view text(e.text.data());
    if (!write_settings_file(text)) {
        set_status(e, "Could not write settings.json. The previous file is unchanged.");
        log_save(false, "write");
        return;
    }
    // The file now holds exactly this text, so it becomes the new baseline for "unsaved edits".
    std::size_t out = 0;
    for (char value : text) {
        e.disk[out++] = value;
    }
    e.diskLength = out;
    set_status(e, "Saved settings.json. Restart the game to apply the changes.");
    log_save(true, "none");
}

void reload(Editor& e) noexcept {
    if (load_from_disk(e)) {
        set_status(e, "Reloaded settings.json from disk.");
    }
}

} // namespace

/** Draws the raw settings.json editor inside the active Core UI frame. */
void draw() noexcept {
    Editor& e = editor();
    if (!e.initialized) {
        e.initialized = true;
        (void)load_from_disk(e);
        if (e.status[0] == '\0') {
            set_status(e, "Loaded settings.json.");
        }
    }

    ImGui::TextUnformatted("Settings JSON");
    ImGui::Separator();
    ImGui::TextWrapped("Edits settings.json as raw text. Save writes the file. Core settings are read "
                       "once at startup, so a running game keeps its values until it restarts.");
    ImGui::Spacing();

    if (ImGui::Button("Reload from Disk")) {
        if (has_unsaved_edits(e) && !e.discardArmed) {
            e.discardArmed = true;
            set_status(e, "Unsaved edits will be replaced. Click Reload from Disk again to confirm.");
        } else {
            reload(e);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Validate")) {
        (void)validate_full(e);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        save(e);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Bundled Defaults (not ported)")) {
        if (!stub_load_bundled_defaults()) {
            set_status(e, "Loading bundled defaults is not ported to Dawn yet.");
        }
    }
    ImGui::Spacing();

    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool edited = ImGui::InputTextMultiline("##settings_json_text",
                                                  e.text.data(),
                                                  e.text.size(),
                                                  ImVec2(-FLT_MIN, kEditorHeight),
                                                  ImGuiInputTextFlags_AllowTabInput);
    if (edited) {
        e.discardArmed = false;
        check_syntax_now(e);
    }

    if (e.syntaxValid) {
        ImGui::TextUnformatted("JSON syntax is valid.");
    } else {
        std::array<char, kLineCapacity> line{};
        format_syntax_error(e, line);
        ImGui::TextUnformatted(line.data());
    }
    ImGui::TextDisabled("%s", has_unsaved_edits(e) ? "Unsaved changes." : "No unsaved changes.");
    if (e.status[0] != '\0') {
        ImGui::TextDisabled("%s", e.status.data());
    }
}

} // namespace dawn::client::ui::settings_json
