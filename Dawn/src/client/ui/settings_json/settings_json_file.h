#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace dawn::client::ui::settings_json {

/** Largest settings.json the editor accepts, matching the Core loader's 1 MiB limit. */
inline constexpr std::size_t kDocumentCapacity = 1024 * 1024;

/**
 * Reads settings.json whole, byte for byte, from the Dawn-owned folder.
 * @param text Receives the bytes. A file larger than the span is refused, never truncated.
 * @param length Receives the byte count when the read succeeds; zero otherwise.
 * @return True when the file opened and fit.
 */
[[nodiscard]] bool read_settings_file(std::span<char> text, std::size_t& length) noexcept;

/**
 * Replaces settings.json. The text is staged beside the file and moved over it, so a failed write
 * leaves the previous file in place. The Core loader reads the file once at startup, so a running
 * game keeps its current values until it restarts.
 * @param text Complete document to store.
 * @return True when the new file is in place.
 */
[[nodiscard]] bool write_settings_file(std::string_view text) noexcept;

/**
 * NOT IMPLEMENTED. Sundial checks the runtime DLL against the document before every write. Dawn has
 * no equivalent. This stub always returns true, so a true result is not a compatibility check.
 * @return Always true.
 */
[[nodiscard]] bool stub_runtime_compatibility_check() noexcept;

/**
 * NOT IMPLEMENTED. Sundial's Load Defaults reads its bundled defaults for completions. Dawn's
 * bundled defaults live in an RCDATA resource read only by the Core loader, and nothing here reads it.
 * @return Always false.
 */
[[nodiscard]] bool stub_load_bundled_defaults() noexcept;

} // namespace dawn::client::ui::settings_json
