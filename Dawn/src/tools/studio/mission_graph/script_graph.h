#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lua_scan.h"

namespace dawn::tools::studio::mission_graph {

/** The kinds of top-level statement a node can show. */
enum class NodeKind : std::uint8_t { module, function, value, code };

/**
 * One top-level statement of the script.
 * @param key Stable key such as "script/function/on_start"; the "script" prefix is the root.
 * @param start First byte of the node's code, including comment lines directly above it.
 * @param end One past the node's last code byte.
 * @param removeStart First byte removed with the node: the start of its line when only spaces precede it.
 * @param removeEnd One past the bytes removed with the node, including the rest of its last line.
 * @param returns True for a return statement, which new statements are inserted before.
 */
struct Node final {
    std::string key;
    NodeKind kind{NodeKind::code};
    std::string title;
    std::string subtitle;
    std::vector<std::string> defines;
    bool hasValue{true};
    std::size_t start{};
    std::size_t end{};
    std::size_t removeStart{};
    std::size_t removeEnd{};
    bool returns{};
};

/** A name's definition linked to a later statement that uses it. */
struct Edge final {
    std::string source;
    std::string target;
    std::string label;
};

/**
 * The script as a graph: each top-level statement is a node, and a name a statement defines links to each
 * later statement that uses it. Nothing here runs Lua. Every edit returns the new script text, which the
 * caller parses again; the graph itself never changes after parse.
 */
class ScriptGraph final {
public:
    ScriptGraph(const ScriptGraph&) = delete;
    ScriptGraph& operator=(const ScriptGraph&) = delete;

    /**
     * Reads a chunk into a graph.
     * @param error Set to the reason when null is returned.
     * @return The graph, or null when the text cannot be tokenized or split into statements.
     */
    [[nodiscard]] static std::unique_ptr<ScriptGraph> parse(std::string_view text, std::string& error) noexcept;

    [[nodiscard]] std::string_view source() const noexcept { return source_; }
    [[nodiscard]] const std::vector<Node>& nodes() const noexcept { return nodes_; }
    [[nodiscard]] const std::vector<Edge>& edges() const noexcept { return edges_; }

    /** The node with this key, or null. */
    [[nodiscard]] const Node* find(std::string_view key) const noexcept;

    /** The node's code as its inspector edits it, comment lines included. */
    [[nodiscard]] std::string_view code(const Node& node) const noexcept;

    /** The source without this node; nullopt when the key is unknown or the result does not parse. */
    [[nodiscard]] std::optional<std::string> without(std::string_view key) const;

    /** The source with this node's code replaced; nullopt when the key is unknown or the result does not parse. */
    [[nodiscard]] std::optional<std::string> with_code(std::string_view key, std::string_view code) const;

    /**
     * The source with a statement added before a final return, or at the end when there is none.
     * @return nullopt when the result does not parse.
     */
    [[nodiscard]] std::optional<std::string> with_statement(std::string_view code) const;

    /**
     * The source with this node swapped with its neighbour. Returns the unchanged source when there is no
     * neighbour in that direction; nullopt when the key is unknown or the result does not parse.
     * @param direction -1 for up, +1 for down.
     */
    [[nodiscard]] std::optional<std::string> moved(std::string_view key, int direction) const;

private:
    ScriptGraph() = default;
    void build(const std::vector<lua_scan::Token>& tokens, const std::vector<lua_scan::Statement>& statements);

    std::string source_;
    std::vector<Node> nodes_;
    std::vector<Edge> edges_;
};

} // namespace dawn::tools::studio::mission_graph
