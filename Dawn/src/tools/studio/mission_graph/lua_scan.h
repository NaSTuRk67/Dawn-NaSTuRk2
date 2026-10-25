#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dawn::tools::studio::mission_graph::lua_scan {

/** Lexical class of one Lua token. Comments and whitespace are never tokens. */
enum class TokenKind : std::uint8_t { name, keyword, number, string, symbol };

/** One token; start and end are byte offsets into the scanned text, end exclusive. */
struct Token final {
    TokenKind kind{};
    std::size_t start{};
    std::size_t end{};
};

/**
 * One top-level statement, as token indices and byte offsets.
 * @param start First byte of its code; comment lines above it are not included.
 * @param end One past its last code byte.
 * @param first Index of its first token.
 * @param last One past the index of its last token.
 */
struct Statement final {
    std::size_t start{};
    std::size_t end{};
    std::size_t first{};
    std::size_t last{};
};

/** True for a Lua 5.4 reserved word. */
[[nodiscard]] bool is_keyword(std::string_view word) noexcept;

/** The source text of one token. */
[[nodiscard]] std::string_view text_of(std::string_view source, const Token& token) noexcept;

/**
 * Splits text into tokens. A leading UTF-8 byte order mark is skipped.
 * @param error Set to "line N: reason" when false is returned.
 * @return False for an unfinished string or long bracket, or a byte that is not Lua.
 */
[[nodiscard]] bool tokenize(std::string_view text, std::vector<Token>& tokens, std::string& error);

/**
 * Splits tokens into top-level statements. Blocks (function, if, do, repeat) and brackets keep a statement
 * open. Outside them a new statement starts on a later line, unless the previous token continues an
 * expression or the next token is an operator or a bracket, which Lua also reads as a continuation.
 * @param error Set to the reason when false is returned.
 * @return False when a block or bracket is closed without being opened, or is left open at the end.
 */
[[nodiscard]] bool split(std::string_view text, const std::vector<Token>& tokens, std::vector<Statement>& statements,
                         std::string& error);

} // namespace dawn::tools::studio::mission_graph::lua_scan
