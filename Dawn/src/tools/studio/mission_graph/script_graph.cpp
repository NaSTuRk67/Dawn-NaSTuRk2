/** The script graph: statements as nodes, definition-to-use links, and text edits that return new source. */

#include "script_graph.h"

#include <algorithm>
#include <exception>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace dawn::tools::studio::mission_graph {
namespace {

using lua_scan::Statement;
using lua_scan::Token;
using lua_scan::TokenKind;

constexpr std::string_view kRoot = "script";
constexpr std::size_t kNone = std::string_view::npos;

bool is_space(char c) noexcept { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

std::string_view word(std::string_view text, const Token& token) noexcept { return lua_scan::text_of(text, token); }

/** Offset of the first byte of the line that holds at. */
std::size_t line_start(std::string_view text, std::size_t at) noexcept {
    if (at == 0) return 0;
    const std::size_t newline = text.rfind('\n', at - 1);
    return newline == kNone ? 0 : newline + 1;
}

/** True when only spaces and tabs come between the start of at's line and at. */
bool starts_line(std::string_view text, std::size_t at) noexcept {
    const std::size_t begin = line_start(text, at);
    return text.substr(begin, at - begin).find_first_not_of(" \t") == kNone;
}

/**
 * The first byte of the comment lines directly above a statement that starts its line. Comment lines never
 * reach above the previous statement, which ends at previousEnd.
 */
std::size_t leading_start(std::string_view text, std::size_t start, std::size_t previousEnd) noexcept {
    if (!starts_line(text, start)) return start;
    std::size_t first = start;
    std::size_t line = line_start(text, start);
    while (line > previousEnd) {
        const std::size_t above = line_start(text, line - 1);
        if (above < previousEnd) break;
        const std::string_view row = text.substr(above, line - 1 - above);
        const std::size_t at = row.find_first_not_of(" \t\r");
        if (at == kNone || row.substr(at, 2) != "--") break;
        first = above + at;
        line = above;
    }
    return first;
}

/** One past the bytes removed with a statement: separators and the rest of its line, when nothing else follows. */
std::size_t removal_end(std::string_view text, std::size_t end) noexcept {
    std::size_t at = end;
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == ',' || text[at] == ';')) ++at;
    if (at < text.size() && text[at] == '\r') ++at;
    if (at < text.size() && text[at] == '\n') return at + 1;
    return end;
}

/** Bracket depth change of one token: +1 opens, -1 closes, 0 otherwise. */
int bracket(std::string_view text, const Token& token) noexcept {
    const auto value = word(text, token);
    if (token.kind != TokenKind::symbol) return 0;
    if (value == "(" || value == "{" || value == "[") return 1;
    if (value == ")" || value == "}" || value == "]") return -1;
    return 0;
}

/** The source of tokens [from, to) with whitespace runs collapsed and cut at a short length. */
std::string snippet(std::string_view text, const std::vector<Token>& tokens, std::size_t from, std::size_t to) {
    constexpr std::size_t kLength = 48;
    std::string out;
    if (to <= from) return out;
    const std::string_view raw = text.substr(tokens[from].start, tokens[to - 1].end - tokens[from].start);
    bool gap = false;
    for (const char c : raw) {
        if (is_space(c)) {
            gap = !out.empty();
            continue;
        }
        if (gap) out.push_back(' ');
        gap = false;
        out.push_back(c);
        if (out.size() >= kLength) return out + "...";
    }
    return out;
}

/** Parameter names from the '(' at open up to its ')', joined with ", ". */
std::string parameters(std::string_view text, const std::vector<Token>& tokens, std::size_t open, std::size_t last) {
    std::string out;
    for (std::size_t i = open + 1; i < last && word(text, tokens[i]) != ")"; ++i) {
        out += word(text, tokens[i]) == "," ? std::string(", ") : std::string(word(text, tokens[i]));
    }
    return out;
}

std::string signature(const std::string& name, const std::string& params) { return name + "(" + params + ")"; }

std::string join(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) out += ", ";
        out += names[i];
    }
    return out;
}

/** A plain name, such as "on_start", rather than a dotted or colon name such as "M.on_start". */
bool is_identifier(std::string_view name) noexcept { return !name.empty() && name.find_first_of(".: ") == kNone; }

/** Unique node key under the root: a second statement with the same base gets "#2", "#3" and so on. */
std::string unique_key(std::unordered_set<std::string>& keys, const std::string& base) {
    std::string result = std::string(kRoot) + "/" + base;
    for (int n = 2; !keys.insert(result).second; ++n) result = std::string(kRoot) + "/" + base + "#" + std::to_string(n);
    return result;
}

/**
 * True when the name at token i is a use rather than a field name or a member after '.' or ':'.
 * @param depth Bracket depth at token i, counted from first.
 */
bool is_reference(std::string_view text, const std::vector<Token>& tokens, std::size_t i, std::size_t first, int depth) noexcept {
    if (tokens[i].kind != TokenKind::name) return false;
    if (i > 0) {
        const auto before = word(text, tokens[i - 1]);
        if (before == "." || before == ":") return false;
    }
    if (i > first && depth > 0 && i + 1 < tokens.size() && word(text, tokens[i + 1]) == "=") {
        const auto before = word(text, tokens[i - 1]);
        if (before == "{" || before == "," || before == ";") return false;
    }
    return true;
}

/**
 * Classifies one statement and fills everything but its links. The key is made unique among keys.
 * @param previousEnd End of the statement before this one, so comment lines are read only below it.
 */
Node describe(std::string_view text, const std::vector<Token>& tokens, const Statement& statement, std::size_t previousEnd,
              std::unordered_map<std::string, int>& ordinals, std::unordered_set<std::string>& keys) {
    Node node;
    const std::size_t first = statement.first, last = statement.last;
    node.start = leading_start(text, statement.start, previousEnd);
    node.end = statement.end;
    node.removeStart = starts_line(text, node.start) ? line_start(text, node.start) : node.start;
    node.removeEnd = removal_end(text, statement.end);
    node.returns = word(text, tokens[first]) == "return";

    const bool local = word(text, tokens[first]) == "local";
    const std::size_t head = local ? first + 1 : first;
    std::string base;

    if (head < last && word(text, tokens[head]) == "function") {
        // function name(...) and local function name(...)
        std::string name;
        std::size_t open = head + 1;
        while (open < last && word(text, tokens[open]) != "(") name += std::string(word(text, tokens[open++]));
        node.kind = NodeKind::function;
        node.subtitle = "function";
        node.title = signature(name, open < last ? parameters(text, tokens, open, last) : std::string());
        if (is_identifier(name)) node.defines = {name};
        base = "function/" + name;
    } else {
        // local a, b [= values] and a, b = values; a statement that is only a name list, such as a call, is code.
        std::vector<std::string> names;
        std::size_t cursor = head;
        while (cursor < last && tokens[cursor].kind == TokenKind::name) {
            names.emplace_back(word(text, tokens[cursor]));
            ++cursor;
            if (cursor < last && word(text, tokens[cursor]) == ",") {
                ++cursor;
                continue;
            }
            break;
        }
        const bool equals = cursor < last && word(text, tokens[cursor]) == "=";
        if (!names.empty() && (local || equals)) {
            const std::size_t value = cursor + 1;
            node.defines = names;
            node.hasValue = equals;
            if (equals && names.size() == 1 && value < last && word(text, tokens[value]) == "function") {
                const std::size_t open = value + 1;
                node.kind = NodeKind::function;
                node.subtitle = "function";
                node.title = signature(names[0], open < last && word(text, tokens[open]) == "(" ? parameters(text, tokens, open, last)
                                                                                                  : std::string());
                base = "function/" + names[0];
            } else if (equals && value < last && word(text, tokens[value]) == "require") {
                node.kind = NodeKind::module;
                node.subtitle = "module";
                node.title = join(names);
                base = "module/" + names[0];
            } else {
                node.kind = NodeKind::value;
                node.subtitle = "value";
                node.title = (local ? std::string("local ") : std::string()) + join(names);
                base = "value/" + names[0];
            }
        }
    }

    if (base.empty()) {
        // Any other statement is code, titled by its first tokens.
        const auto keyword = word(text, tokens[first]);
        const bool isKeyword = lua_scan::is_keyword(keyword);
        const std::string label = isKeyword ? std::string(keyword) : std::string("call");
        const int ordinal = ++ordinals[label];
        node.kind = NodeKind::code;
        base = "code/" + label + std::to_string(ordinal);
        if (keyword == "if" || keyword == "for" || keyword == "while") {
            std::size_t stop = first + 1;
            while (stop < last && word(text, tokens[stop]) != "then" && word(text, tokens[stop]) != "do") ++stop;
            node.title = std::string(keyword) + " " + snippet(text, tokens, first + 1, stop);
            node.subtitle = keyword == "if" ? "if / branches" : keyword == "for" ? "for loop" : "while loop";
        } else if (keyword == "repeat" || keyword == "do") {
            node.title = std::string(keyword) + " block";
            node.subtitle = keyword == "repeat" ? "repeat loop" : "do block";
        } else if (keyword == "return") {
            node.title = last - first > 1 ? "return " + snippet(text, tokens, first + 1, last) : std::string("return");
            node.subtitle = "return";
        } else {
            node.title = snippet(text, tokens, first, last);
            node.subtitle = isKeyword ? std::string(keyword) : std::string("call");
        }
    }
    node.key = unique_key(keys, base);
    return node;
}

/** Makes an edit's result a script only when it still tokenizes and splits into statements. */
std::optional<std::string> checked(std::string next) {
    std::string error;
    if (!ScriptGraph::parse(next, error)) return std::nullopt;
    return next;
}

} // namespace

std::unique_ptr<ScriptGraph> ScriptGraph::parse(std::string_view text, std::string& error) noexcept {
    try {
        error.clear();
        std::vector<Token> tokens;
        std::vector<Statement> statements;
        if (!lua_scan::tokenize(text, tokens, error) || !lua_scan::split(text, tokens, statements, error)) return nullptr;
        std::unique_ptr<ScriptGraph> graph(new ScriptGraph);
        graph->source_.assign(text.data(), text.size());
        graph->build(tokens, statements);
        return graph;
    } catch (const std::exception& failure) {
        error = failure.what();
        return nullptr;
    }
}

void ScriptGraph::build(const std::vector<Token>& tokens, const std::vector<Statement>& statements) {
    std::unordered_map<std::string, int> ordinals;
    std::unordered_set<std::string> keys;
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    std::size_t previousEnd = 0;
    nodes_.reserve(statements.size());
    for (const Statement& statement : statements) {
        nodes_.push_back(describe(source_, tokens, statement, previousEnd, ordinals, keys));
        ranges.emplace_back(statement.first, statement.last);
        previousEnd = statement.end;
    }

    // A name's definition links to each later statement that uses it. The last definition of a name wins,
    // except that a declaration without a value does not replace an earlier definition.
    std::unordered_map<std::string, std::size_t> definers;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        for (const auto& name : nodes_[i].defines) {
            if (nodes_[i].hasValue || !definers.contains(name)) definers[name] = i;
        }
    }
    const std::string_view text = source_;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const Node& user = nodes_[i];
        const auto [first, last] = ranges[i];
        std::unordered_set<std::string> seen;
        int depth = 0;
        for (std::size_t t = first; t < last; ++t) {
            if (tokens[t].kind == TokenKind::name && is_reference(text, tokens, t, first, depth)) {
                const std::string name(word(text, tokens[t]));
                const bool own = std::find(user.defines.begin(), user.defines.end(), name) != user.defines.end();
                const auto found = definers.find(name);
                if (!own && found != definers.end() && found->second != i && seen.insert(name).second) {
                    edges_.push_back({nodes_[found->second].key, user.key, name});
                }
            }
            depth += bracket(text, tokens[t]);
        }
    }
}

const Node* ScriptGraph::find(std::string_view key) const noexcept {
    for (const Node& node : nodes_) {
        if (node.key == key) return &node;
    }
    return nullptr;
}

std::string_view ScriptGraph::code(const Node& node) const noexcept {
    return std::string_view(source_).substr(node.start, node.end - node.start);
}

std::optional<std::string> ScriptGraph::without(std::string_view key) const {
    const Node* node = find(key);
    if (node == nullptr) return std::nullopt;
    return checked(source_.substr(0, node->removeStart) + source_.substr(node->removeEnd));
}

std::optional<std::string> ScriptGraph::with_code(std::string_view key, std::string_view code) const {
    const Node* node = find(key);
    if (node == nullptr) return std::nullopt;
    return checked(source_.substr(0, node->start) + std::string(code) + source_.substr(node->end));
}

std::optional<std::string> ScriptGraph::with_statement(std::string_view code) const {
    const std::string newline = source_.find("\r\n") == kNone ? "\n" : "\r\n";
    if (!nodes_.empty() && nodes_.back().returns) {
        const std::size_t at = nodes_.back().start;
        return checked(source_.substr(0, at) + std::string(code) + newline + newline + source_.substr(at));
    }
    std::string head(source_);
    while (!head.empty() && is_space(head.back())) head.pop_back();
    if (head.empty()) return checked(std::string(code) + newline);
    return checked(head + newline + newline + std::string(code) + newline);
}

std::optional<std::string> ScriptGraph::moved(std::string_view key, int direction) const {
    std::size_t index = 0;
    while (index < nodes_.size() && nodes_[index].key != key) ++index;
    if (index == nodes_.size()) return std::nullopt;
    const std::ptrdiff_t other = static_cast<std::ptrdiff_t>(index) + (direction < 0 ? -1 : 1);
    if (other < 0 || other >= static_cast<std::ptrdiff_t>(nodes_.size())) return source_;

    const auto neighbour = static_cast<std::size_t>(other);
    const Node& first = nodes_[std::min(index, neighbour)];
    const Node& second = nodes_[std::max(index, neighbour)];
    const std::string_view text = source_;
    // Swap the two statements with their comment lines; the text between them stays where it was.
    std::string next(text.substr(0, first.start));
    next.append(text.substr(second.start, second.end - second.start));
    next.append(text.substr(first.end, second.start - first.end));
    next.append(text.substr(first.start, first.end - first.start));
    next.append(text.substr(second.end));
    return checked(std::move(next));
}

} // namespace dawn::tools::studio::mission_graph
