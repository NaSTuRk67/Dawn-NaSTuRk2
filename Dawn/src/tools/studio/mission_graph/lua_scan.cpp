/** A small Lua 5.4 lexer and top-level statement splitter. It never runs Lua; it only finds where statements are. */

#include "lua_scan.h"

#include <array>

namespace dawn::tools::studio::mission_graph::lua_scan {
namespace {

using std::size_t;
constexpr size_t kNone = std::string_view::npos;

// Lua 5.4 reserved words.
constexpr std::array<std::string_view, 22> kKeywords{
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
    "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};

// Two- and three-character operators, longest first within each prefix.
constexpr std::array<std::string_view, 10> kMultiSymbols{"...", "..", "==", "~=", "<=", ">=", "::", "//", "<<", ">>"};

constexpr std::string_view kSingleSymbols = "+-*/%^#&~|<>=(){}[];:,.";
constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";

bool is_space(char c) noexcept { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
bool is_name_start(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool is_name_char(char c) noexcept { return is_name_start(c) || is_digit(c); }

/** Line number of a byte offset, counted from 1. */
size_t line_of(std::string_view text, size_t at) noexcept {
    size_t line = 1;
    for (size_t i = 0; i < at && i < text.size(); ++i) line += text[i] == '\n';
    return line;
}

/** Level of a long bracket that opens at pos ("[", zero or more "=", "["), or -1 when there is none. */
int long_open(std::string_view text, size_t pos) noexcept {
    if (pos >= text.size() || text[pos] != '[') return -1;
    size_t at = pos + 1;
    int level = 0;
    while (at < text.size() && text[at] == '=') { ++at; ++level; }
    return at < text.size() && text[at] == '[' ? level : -1;
}

/** One past the closing bracket of the given level, searching from pos, or kNone when unfinished. */
size_t long_close(std::string_view text, size_t from, int level) {
    const std::string closing = "]" + std::string(static_cast<size_t>(level), '=') + "]";
    const size_t at = text.find(closing, from);
    return at == kNone ? kNone : at + closing.size();
}

bool is_continuation_word(std::string_view word) noexcept {
    return word == "return" || word == "local" || word == "not" || word == "and" || word == "or" || word == "in"
        || word == "goto";
}

/** A token that ends an expression, so the next line may begin a new statement. */
bool ends_expression(std::string_view word, TokenKind kind) noexcept {
    if (kind == TokenKind::symbol) return word == ")" || word == "]" || word == "}" || word == "..." || word == "::";
    if (kind == TokenKind::keyword) return word == "end" || word == "nil" || word == "true" || word == "false" || word == "break";
    return true;
}

} // namespace

bool is_keyword(std::string_view word) noexcept {
    for (const auto keyword : kKeywords) {
        if (keyword == word) return true;
    }
    return false;
}

std::string_view text_of(std::string_view source, const Token& token) noexcept {
    return source.substr(token.start, token.end - token.start);
}

bool tokenize(std::string_view text, std::vector<Token>& tokens, std::string& error) {
    tokens.clear();
    error.clear();
    size_t i = text.starts_with(kByteOrderMark) ? kByteOrderMark.size() : 0;
    const auto fail = [&](size_t at, std::string_view reason) {
        error = "line " + std::to_string(line_of(text, at)) + ": " + std::string(reason);
        return false;
    };
    while (i < text.size()) {
        const char c = text[i];
        if (is_space(c)) { ++i; continue; }

        if (c == '-' && i + 1 < text.size() && text[i + 1] == '-') {
            const int level = long_open(text, i + 2);
            if (level >= 0) {
                const size_t end = long_close(text, i + 2 + static_cast<size_t>(level) + 2, level);
                if (end == kNone) return fail(i, "unfinished long comment");
                i = end;
                continue;
            }
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }

        if (c == '[') {
            const int level = long_open(text, i);
            if (level >= 0) {
                const size_t end = long_close(text, i + static_cast<size_t>(level) + 2, level);
                if (end == kNone) return fail(i, "unfinished long string");
                tokens.push_back({TokenKind::string, i, end});
                i = end;
                continue;
            }
        }

        if (c == '"' || c == '\'') {
            size_t j = i + 1;
            while (j < text.size() && text[j] != c) {
                if (text[j] == '\n') return fail(i, "unfinished string");
                j += text[j] == '\\' ? 2 : 1;
            }
            if (j >= text.size()) return fail(i, "unfinished string");
            tokens.push_back({TokenKind::string, i, j + 1});
            i = j + 1;
            continue;
        }

        if (is_digit(c) || (c == '.' && i + 1 < text.size() && is_digit(text[i + 1]))) {
            size_t j = i + 1;
            while (j < text.size()) {
                const char d = text[j];
                if (d == '.' && j + 1 < text.size() && text[j + 1] == '.') break;
                if (is_name_char(d) || d == '.') {
                    ++j;
                } else if ((d == '+' || d == '-') && (text[j - 1] == 'e' || text[j - 1] == 'E' || text[j - 1] == 'p'
                                                     || text[j - 1] == 'P')) {
                    ++j;
                } else {
                    break;
                }
            }
            tokens.push_back({TokenKind::number, i, j});
            i = j;
            continue;
        }

        if (is_name_start(c)) {
            size_t j = i + 1;
            while (j < text.size() && is_name_char(text[j])) ++j;
            const auto word = text.substr(i, j - i);
            tokens.push_back({is_keyword(word) ? TokenKind::keyword : TokenKind::name, i, j});
            i = j;
            continue;
        }

        size_t width = 0;
        for (const auto symbol : kMultiSymbols) {
            if (text.substr(i).starts_with(symbol)) { width = symbol.size(); break; }
        }
        if (width == 0) {
            if (kSingleSymbols.find(c) == kNone) return fail(i, "unexpected character");
            width = 1;
        }
        tokens.push_back({TokenKind::symbol, i, i + width});
        i += width;
    }
    return true;
}

bool split(std::string_view text, const std::vector<Token>& tokens, std::vector<Statement>& statements, std::string& error) {
    statements.clear();
    error.clear();
    int blocks = 0, brackets = 0;
    bool open = false;
    size_t first = 0;

    const auto close = [&](size_t next) {
        statements.push_back({tokens[first].start, tokens[next - 1].end, first, next});
        open = false;
    };
    // A new statement may start here: the token is on a later line and nothing continues the expression.
    const auto begins_new = [&](size_t i) {
        const Token& previous = tokens[i - 1];
        const Token& current = tokens[i];
        if (text.substr(previous.end, current.start - previous.end).find('\n') == kNone) return false;
        const auto before = text_of(text, previous);
        if (previous.kind == TokenKind::symbol ? !ends_expression(before, previous.kind) : (previous.kind == TokenKind::keyword
            && is_continuation_word(before))) return false;
        const auto word = text_of(text, current);
        if (current.kind == TokenKind::symbol) return word == "::";
        if (current.kind == TokenKind::keyword) return word != "and" && word != "or" && word != "in" && word != "then" && word != "do";
        return true;
    };

    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto word = text_of(text, tokens[i]);
        if (blocks == 0 && brackets == 0) {
            if (word == ";") {
                if (open) close(i);
                continue;
            }
            if (open && begins_new(i)) close(i);
        }
        if (!open) {
            first = i;
            open = true;
        }
        if (word == "function" || word == "if" || word == "do" || word == "repeat") ++blocks;
        if (word == "end" || word == "until") {
            if (--blocks < 0) { error = "line " + std::to_string(line_of(text, tokens[i].start)) + ": unexpected '" + std::string(word) + "'"; return false; }
        }
        if (word == "(" || word == "{" || word == "[") ++brackets;
        if (word == ")" || word == "}" || word == "]") {
            if (--brackets < 0) { error = "line " + std::to_string(line_of(text, tokens[i].start)) + ": unexpected '" + std::string(word) + "'"; return false; }
        }
    }
    if (blocks != 0) { error = "unfinished block at the end of the script"; return false; }
    if (brackets != 0) { error = "unclosed bracket at the end of the script"; return false; }
    if (open) close(tokens.size());
    return true;
}

} // namespace dawn::tools::studio::mission_graph::lua_scan
