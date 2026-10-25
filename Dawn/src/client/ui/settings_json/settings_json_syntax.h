#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace dawn::client::ui::settings_json {

/** Containers may nest this deep. Matches the Sundial strict reader: a value at depth 16 is refused. */
inline constexpr unsigned kMaximumNestingDepth = 16;
/** Member names compared for duplicates in one object. A larger object is refused, not skipped. */
inline constexpr std::size_t kMaximumObjectMembers = 1024;
/** Fixed bytes for a syntax error reason, including the null terminator. */
inline constexpr std::size_t kSyntaxMessageCapacity = 96;

/** Where and why a document stopped being strict JSON. */
struct SyntaxError {
    /** Byte offset of the first byte that could not be read. */
    std::size_t offset{};
    /** One-based line of offset. */
    std::size_t line{};
    /** One-based byte column of offset. */
    std::size_t column{};
    /** Null-terminated fixed reason text. */
    std::array<char, kSyntaxMessageCapacity> message{};
};

/**
 * Checks that the text is exactly one strict JSON value.
 * Rejects control bytes in strings, duplicate member names in one object, nesting past the
 * limit, and any content after the value. Duplicate names are compared as raw bytes, so names
 * that differ only by escape spelling are not detected.
 *
 * Uses function-local static storage for the name tables. Call from one thread only.
 * @param text Complete document, borrowed.
 * @param error Receives the location and reason when the check fails.
 * @return True when the text is strict JSON.
 */
[[nodiscard]] bool check_syntax(std::string_view text, SyntaxError& error) noexcept;

} // namespace dawn::client::ui::settings_json
