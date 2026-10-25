/**
 * settings.json file access for the raw editor. Same file and folder as the Core loader in
 * core/settings/settings_runtime.cpp, reached through the shared path helpers.
 */

#include "settings_json_file.h"

#include <Windows.h>

#include <cstdint>
#include <limits>

#include "../../../core/filesystem/path.h"

namespace dawn::client::ui::settings_json {
namespace {

/** The file the Core loader reads. Matches settings_runtime.cpp. */
constexpr std::wstring_view kSettingsFile = L"settings.json";
/** Staging name beside the file. CREATE_ALWAYS replaces any leftover from a failed write. */
constexpr std::wstring_view kStagingFile = L"settings.json.editor.tmp";

} // namespace

/** Reads settings.json whole, byte for byte. */
bool read_settings_file(std::span<char> text, std::size_t& length) noexcept {
    length = 0;
    core::path::Buffer file{};
    if (!core::path::artifact_file(kSettingsFile, file)) {
        return false;
    }
    const HANDLE handle = CreateFileW(file.chars.data(),
                                      GENERIC_READ,
                                      FILE_SHARE_READ,
                                      nullptr,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL,
                                      nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    const bool measured = GetFileSizeEx(handle, &size) != FALSE && size.QuadPart >= 0;
    const bool fits = measured
                      && static_cast<std::uint64_t>(size.QuadPart) <= text.size()
                      && static_cast<std::uint64_t>(size.QuadPart)
                             <= (std::numeric_limits<DWORD>::max)();
    DWORD read = 0;
    const bool ok = fits
                    && ReadFile(handle,
                                text.data(),
                                static_cast<DWORD>(size.QuadPart),
                                &read,
                                nullptr) != FALSE
                    && static_cast<std::uint64_t>(read) == static_cast<std::uint64_t>(size.QuadPart);
    (void)CloseHandle(handle);
    if (!ok) {
        return false;
    }
    length = read;
    return true;
}

/** Replaces settings.json through a staged sibling, so a failed write keeps the old file. */
bool write_settings_file(std::string_view text) noexcept {
    if (text.size() > (std::numeric_limits<DWORD>::max)()) {
        return false;
    }
    core::path::Buffer staging{};
    core::path::Buffer target{};
    if (!core::path::artifact_file(kStagingFile, staging)
        || !core::path::artifact_file(kSettingsFile, target)) {
        return false;
    }
    const HANDLE handle = CreateFileW(staging.chars.data(),
                                      GENERIC_WRITE,
                                      0,
                                      nullptr,
                                      CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL,
                                      nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const auto size = static_cast<DWORD>(text.size());
    bool complete = WriteFile(handle, text.data(), size, &written, nullptr) != FALSE
                    && written == size;
    complete = CloseHandle(handle) != FALSE && complete;
    complete = complete
               && MoveFileExW(staging.chars.data(),
                              target.chars.data(),
                              MOVEFILE_REPLACE_EXISTING)
                      != FALSE;
    if (!complete) {
        (void)DeleteFileW(staging.chars.data());
    }
    return complete;
}

/** NOT IMPLEMENTED. Always true. See the header. */
bool stub_runtime_compatibility_check() noexcept {
    return true;
}

/** NOT IMPLEMENTED. Always false. See the header. */
bool stub_load_bundled_defaults() noexcept {
    return false;
}

} // namespace dawn::client::ui::settings_json
