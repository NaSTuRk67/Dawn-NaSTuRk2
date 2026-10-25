/**
 * Strict JSON syntax check for the raw settings editor. Port of the Sundial strict reader's rules,
 * written as a plain recursive-descent checker because Dawn has no generic JSON DOM. It validates
 * syntax only; the supported-settings check is dawn::core::settings::parse, run by the panel.
 */

#include "settings_json_syntax.h"

#include <cstdio>

namespace dawn::client::ui::settings_json {
namespace {

/** JSON separates tokens with only these four bytes. */
[[nodiscard]] constexpr bool is_whitespace(char value) noexcept {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

[[nodiscard]] constexpr bool is_digit(char value) noexcept {
    return value >= '0' && value <= '9';
}

[[nodiscard]] constexpr bool is_hex_digit(char value) noexcept {
    return is_digit(value) || (value >= 'A' && value <= 'F') || (value >= 'a' && value <= 'f');
}

/** Control bytes below this value must be escaped inside a JSON string. */
constexpr unsigned char kFirstPrintableByte = 0x20;
/** Escape letters JSON allows after a backslash, besides the Unicode form. */
constexpr std::string_view kSimpleEscapes = "\"\\/bfnrt";
/** Unicode escapes carry exactly four hex digits. */
constexpr int kUnicodeEscapeDigits = 4;

/** Member names already seen in one object, borrowed from the text being checked. */
struct MemberNames {
    std::array<std::string_view, kMaximumObjectMembers> names{};
    std::size_t count{};
};

class Checker {
public:
    /**
     * Checks one whole document.
     * @param text Complete document, borrowed.
     * @param error Receives the failure location.
     * @return True when the document is strict JSON.
     */
    [[nodiscard]] bool run(std::string_view text, SyntaxError& error) noexcept {
        input_ = text;
        position_ = 0;
        error_ = &error;
        error = SyntaxError{};
        if (!value(0)) {
            return false;
        }
        whitespace();
        if (position_ != input_.size()) {
            return fail("unexpected content after the JSON value");
        }
        return true;
    }

private:
    /**
     * Reads one value. A container at depth 15 may still hold only empty children; its children
     * are seen at depth 16, which is refused, matching the Sundial reader.
     */
    [[nodiscard]] bool value(unsigned depth) noexcept {
        if (depth >= kMaximumNestingDepth) {
            return fail("nesting is deeper than the supported limit");
        }
        whitespace();
        if (position_ >= input_.size()) {
            return fail("unexpected end of input");
        }
        const char first = input_[position_];
        if (first == '{') {
            return object(depth);
        }
        if (first == '[') {
            return array(depth);
        }
        if (first == '"') {
            std::string_view ignored;
            return string(ignored);
        }
        if (first == 't') {
            return literal("true");
        }
        if (first == 'f') {
            return literal("false");
        }
        if (first == 'n') {
            return literal("null");
        }
        if (first == '-' || is_digit(first)) {
            return number();
        }
        return fail("unexpected character where a JSON value starts");
    }

    [[nodiscard]] bool object(unsigned depth) noexcept {
        ++position_;
        MemberNames& seen = members_[depth];
        seen.count = 0;
        if (consume('}')) {
            return true;
        }
        for (;;) {
            whitespace();
            const std::size_t nameStart = position_;
            if (position_ >= input_.size() || input_[position_] != '"') {
                return fail("expected an object member name");
            }
            std::string_view name;
            if (!string(name)) {
                return false;
            }
            if (seen.count == kMaximumObjectMembers) {
                return fail("object has more members than the checker can compare");
            }
            for (std::size_t index = 0; index < seen.count; ++index) {
                if (seen.names[index] == name) {
                    return fail_at(nameStart, "duplicate object member name");
                }
            }
            seen.names[seen.count++] = name;
            if (!consume(':')) {
                return fail("expected ':' after the member name");
            }
            if (!value(depth + 1)) {
                return false;
            }
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                return fail("expected ',' or '}' in object");
            }
        }
    }

    [[nodiscard]] bool array(unsigned depth) noexcept {
        ++position_;
        if (consume(']')) {
            return true;
        }
        for (;;) {
            if (!value(depth + 1)) {
                return false;
            }
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return fail("expected ',' or ']' in array");
            }
        }
    }

    /** Reads one string. The caller has checked that the next byte is the opening quote. */
    [[nodiscard]] bool string(std::string_view& raw) noexcept {
        const std::size_t start = ++position_;
        while (position_ < input_.size()) {
            const char current = input_[position_];
            if (current == '"') {
                raw = input_.substr(start, position_ - start);
                ++position_;
                return true;
            }
            if (static_cast<unsigned char>(current) < kFirstPrintableByte) {
                return fail("control character inside a string");
            }
            if (current == '\\') {
                ++position_;
                if (position_ >= input_.size()) {
                    break;
                }
                const char escape = input_[position_];
                if (escape == 'u') {
                    for (int digit = 0; digit < kUnicodeEscapeDigits; ++digit) {
                        ++position_;
                        if (position_ >= input_.size() || !is_hex_digit(input_[position_])) {
                            return fail("invalid \\u escape");
                        }
                    }
                } else if (kSimpleEscapes.find(escape) == std::string_view::npos) {
                    return fail("invalid escape sequence");
                }
            }
            ++position_;
        }
        return fail("unterminated string");
    }

    [[nodiscard]] bool number() noexcept {
        if (input_[position_] == '-') {
            ++position_;
        }
        if (position_ >= input_.size()) {
            return fail("expected a digit after '-'");
        }
        if (input_[position_] == '0') {
            ++position_;
        } else if (is_digit(input_[position_])) {
            (void)digits();
        } else {
            return fail("invalid number");
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            if (!digits()) {
                return fail("expected a digit after the decimal point");
            }
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size()
                && (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            if (!digits()) {
                return fail("expected a digit in the exponent");
            }
        }
        return true;
    }

    /** Reads one or more decimal digits. @return False when none is present. */
    [[nodiscard]] bool digits() noexcept {
        if (position_ >= input_.size() || !is_digit(input_[position_])) {
            return false;
        }
        while (position_ < input_.size() && is_digit(input_[position_])) {
            ++position_;
        }
        return true;
    }

    [[nodiscard]] bool literal(std::string_view expected) noexcept {
        if (input_.substr(position_, expected.size()) != expected) {
            return fail("invalid literal");
        }
        position_ += expected.size();
        return true;
    }

    /** Consumes one expected byte after whitespace. @return False, consuming nothing, otherwise. */
    [[nodiscard]] bool consume(char expected) noexcept {
        whitespace();
        if (position_ >= input_.size() || input_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    void whitespace() noexcept {
        while (position_ < input_.size() && is_whitespace(input_[position_])) {
            ++position_;
        }
    }

    [[nodiscard]] bool fail(const char* reason) noexcept {
        return fail_at(position_, reason);
    }

    /** Records the location and reason. Line and column are counted from the start of the text. */
    [[nodiscard]] bool fail_at(std::size_t offset, const char* reason) noexcept {
        std::size_t line = 1;
        std::size_t lineStart = 0;
        for (std::size_t index = 0; index < offset && index < input_.size(); ++index) {
            if (input_[index] == '\n') {
                ++line;
                lineStart = index + 1;
            }
        }
        error_->offset = offset;
        error_->line = line;
        error_->column = offset - lineStart + 1;
        (void)std::snprintf(error_->message.data(), error_->message.size(), "%s", reason);
        return false;
    }

    std::string_view input_{};
    std::size_t position_{};
    SyntaxError* error_{};
    /** One table per nesting depth. Too large for the call stack, so it lives in static storage. */
    std::array<MemberNames, kMaximumNestingDepth> members_{};
};

} // namespace

/** Checks that the text is exactly one strict JSON value. */
bool check_syntax(std::string_view text, SyntaxError& error) noexcept {
    static Checker checker;
    return checker.run(text, error);
}

} // namespace dawn::client::ui::settings_json
