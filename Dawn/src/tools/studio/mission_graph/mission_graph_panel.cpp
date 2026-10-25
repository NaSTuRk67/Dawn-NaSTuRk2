/** The mission graph panel: a Lua mission script as a graph, with an inspector for the selected node or link. */

#include "mission_graph_panel.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>

#include "mission_graph_canvas.h"
#include "mission_graph_stubs.h"
#include "script_graph.h"

namespace dawn::tools::studio::mission_graph {
namespace {

// The same limit as the C# reader; a larger script is refused before it is read.
constexpr std::uintmax_t kMaxScriptBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::size_t kUndoLimit = 200;
const ImVec4 kFailColor(1.0F, 0.55F, 0.5F, 1.0F);

// Header tints by kind; the colors are not Sunrise's theme, only chosen to read apart.
constexpr ImU32 kTintModule = IM_COL32(64, 90, 128, 255);
constexpr ImU32 kTintFunction = IM_COL32(107, 77, 140, 255);
constexpr ImU32 kTintBranch = IM_COL32(158, 84, 36, 255);
constexpr ImU32 kTintCode = IM_COL32(56, 115, 77, 255);
constexpr ImU32 kTintValue = IM_COL32(77, 82, 92, 255);

/** Edits the inspector asks for this frame. They are applied after the canvas and inspector have drawn. */
struct Pending final {
    std::string removeKey;
    std::string applyKey;
    std::string applyCode;
    std::string moveKey;
    int moveDirection{};
    std::string dropSource;
    std::string dropTarget;
};

/** Everything the panel shows and keeps between frames. */
struct Panel final {
    std::string path;
    std::string pathDraft;
    std::string newCode;
    std::string codeDraft;
    // The node key whose code the inspector shows; the draft is reloaded when it changes.
    std::string draftKey;
    std::string status;
    bool statusFailed{};
    std::unique_ptr<ScriptGraph> graph;
    std::vector<std::string> undo;
    std::vector<std::string> redo;
    // Links made on the canvas. They are for viewing only and are never written to the Lua.
    std::vector<CanvasEdge> links;
    Canvas canvas;
};

Panel& state() noexcept {
    static Panel panel;
    return panel;
}

void set_status(std::string text, bool failed) {
    Panel& p = state();
    p.status = std::move(text);
    p.statusFailed = failed;
}

/** Keeps a std::string in step with an ImGui text box as the box grows and shrinks. */
int grow_text(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* text = static_cast<std::string*>(data->UserData);
        text->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = text->data();
    }
    return 0;
}

/** A text box that edits the string in place. */
bool text_box(const char* label, std::string& text, ImVec2 size, bool multiline) {
    constexpr ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
    if (multiline) return ImGui::InputTextMultiline(label, text.data(), text.capacity() + 1, size, flags, grow_text, &text);
    return ImGui::InputText(label, text.data(), text.capacity() + 1, flags, grow_text, &text);
}

std::string join(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) out += ", ";
        out += names[i];
    }
    return out;
}

std::string title_of(const ScriptGraph& graph, const std::string& key) {
    if (const Node* node = graph.find(key)) return node->title;
    return key;
}

/** The code a node shows: the code without the comment lines above it. The inspector keeps those lines. */
std::string preview_of(std::string_view code) {
    std::size_t first = 0;
    while (true) {
        const std::size_t end = code.find('\n', first);
        const std::string_view row = code.substr(first, end == std::string_view::npos ? std::string_view::npos : end - first);
        const std::size_t at = row.find_first_not_of(" \t\r");
        if (end == std::string_view::npos || at == std::string_view::npos || row.substr(at, 2) != "--") break;
        first = end + 1;
    }
    return std::string(code.substr(first));
}

ImU32 tint_of(const Node& node) {
    switch (node.kind) {
    case NodeKind::module: return kTintModule;
    case NodeKind::function: return kTintFunction;
    case NodeKind::code:
        if (node.subtitle == "if / branches") return kTintBranch;
        if (node.subtitle.ends_with("loop")) return kTintFunction;
        return kTintCode;
    case NodeKind::value: return kTintValue;
    }
    return kTintValue;
}

/** Reads a Lua script. Only .lua files are read; the extension match ignores ASCII case. */
bool read_script(const std::string& path, std::string& text, std::string& error) {
    const std::filesystem::path file(path);
    std::string extension = file.extension().string();
    for (auto& c : extension) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (extension != ".lua") {
        error = path + ": only .lua mission scripts can be read";
        return false;
    }
    std::error_code code;
    const std::uintmax_t size = std::filesystem::file_size(file, code);
    if (code) {
        error = path + ": cannot read mission script";
        return false;
    }
    if (size > kMaxScriptBytes) {
        error = path + ": Lua source is too large";
        return false;
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        error = path + ": cannot open mission script";
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    if (stream.bad()) {
        error = path + ": cannot read mission script";
        return false;
    }
    return true;
}

/** Makes next the open script. Links to nodes it no longer has are dropped. */
void adopt(std::unique_ptr<ScriptGraph> next) {
    Panel& p = state();
    p.graph = std::move(next);
    p.draftKey.clear();
    p.codeDraft.clear();
    std::erase_if(p.links, [&p](const CanvasEdge& link) {
        return p.graph->find(link.source) == nullptr || p.graph->find(link.target) == nullptr;
    });
}

bool load_file(const std::string& path, std::string& error) {
    std::string text;
    if (!read_script(path, text, error)) {
        set_status(error, true);
        return false;
    }
    auto parsed = ScriptGraph::parse(text, error);
    if (!parsed) {
        set_status(path + ": " + error, true);
        return false;
    }
    Panel& p = state();
    p.path = path;
    p.undo.clear();
    p.redo.clear();
    p.links.clear();
    p.canvas.reset_layout();
    p.canvas.clear_selection();
    p.canvas.frame_all();
    adopt(std::move(parsed));
    set_status("Loaded " + std::filesystem::path(path).filename().string(), false);
    return true;
}

/** Makes one edit as one undo step. An empty result is an edit that could not be made; an unchanged one is no edit. */
void apply(const std::optional<std::string>& next, const char* done) {
    Panel& p = state();
    if (p.graph == nullptr) return;
    if (!next) {
        set_status("That edit does not leave a script this view can read. Nothing changed.", true);
        return;
    }
    if (*next == p.graph->source()) {
        set_status("Nothing to change.", false);
        return;
    }
    std::string error;
    auto parsed = ScriptGraph::parse(*next, error);
    if (!parsed) {
        set_status(error, true);
        return;
    }
    p.undo.push_back(std::string(p.graph->source()));
    if (p.undo.size() > kUndoLimit) p.undo.erase(p.undo.begin());
    p.redo.clear();
    adopt(std::move(parsed));
    set_status(done, false);
}

/** Moves one step back or forward through the undo history. */
void step_history(bool back) {
    Panel& p = state();
    auto& from = back ? p.undo : p.redo;
    auto& to = back ? p.redo : p.undo;
    if (from.empty() || p.graph == nullptr) return;
    std::string text = std::move(from.back());
    from.pop_back();
    std::string error;
    auto parsed = ScriptGraph::parse(text, error);
    if (!parsed) {
        set_status(error, true);
        return;
    }
    to.push_back(std::string(p.graph->source()));
    adopt(std::move(parsed));
    set_status(back ? "Undone." : "Redone.", false);
}

void draw_toolbar(Panel& p) {
    ImGui::SetNextItemWidth(std::max(160.0F, ImGui::GetContentRegionAvail().x - 330.0F));
    (void)text_box("##mission_graph_path", p.pathDraft, ImVec2(0.0F, 0.0F), false);
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::string error;
        (void)load_file(p.pathDraft, error);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(p.undo.empty());
    if (ImGui::Button("Undo")) step_history(true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(p.redo.empty());
    if (ImGui::Button("Redo")) step_history(false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Frame all")) p.canvas.frame_all();
    ImGui::SameLine();
    // Arrange clears every position; the next draw puts the nodes back in rows, in script order.
    if (ImGui::Button("Arrange")) p.canvas.reset_layout();
}

void draw_add(Panel& p) {
    (void)text_box("##mission_graph_new_code", p.newCode, ImVec2(-1.0F, 60.0F), true);
    ImGui::BeginDisabled(p.graph == nullptr || p.newCode.empty());
    if (ImGui::Button("Add statement") && p.graph) {
        const auto next = p.graph->with_statement(p.newCode);
        const bool made = next.has_value();
        apply(next, "Added statement.");
        if (made) p.newCode.clear();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Added before a final return, or at the end.");
}

void draw_link_inspector(Panel& p, const CanvasEdge& link, Pending& pending) {
    ImGui::TextUnformatted("Link");
    ImGui::Separator();
    ImGui::TextWrapped("%s  ->  %s", title_of(*p.graph, link.source).c_str(), title_of(*p.graph, link.target).c_str());
    ImGui::TextDisabled("%s", link.label.c_str());
    if (!link.editable) {
        ImGui::TextWrapped("Set by the Lua: a name one statement defines and a later statement uses. Edit the code to change it.");
        return;
    }
    if (ImGui::Button("Remove link")) {
        pending.dropSource = link.source;
        pending.dropTarget = link.target;
    }
    ImGui::TextWrapped("Removing a link changes only this view. The Lua is not touched.");
}

void draw_node_inspector(Panel& p, const Node& node, Pending& pending) {
    ImGui::TextUnformatted(node.title.c_str());
    ImGui::TextDisabled("%s", node.subtitle.c_str());
    if (!node.defines.empty()) ImGui::TextWrapped("Defines: %s", join(node.defines).c_str());
    ImGui::Separator();
    // The draft is the code as the node holds it, comment lines included. Apply writes it back.
    (void)text_box("##mission_graph_code", p.codeDraft, ImVec2(-1.0F, 220.0F), true);
    if (ImGui::Button("Apply")) {
        pending.applyKey = node.key;
        pending.applyCode = p.codeDraft;
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) p.codeDraft = std::string(p.graph->code(node));
    ImGui::Separator();
    if (ImGui::Button("Move up")) {
        pending.moveKey = node.key;
        pending.moveDirection = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Move down")) {
        pending.moveKey = node.key;
        pending.moveDirection = 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) pending.removeKey = node.key;
    ImGui::TextDisabled("Delete also removes the comment lines above the node.");
}

void draw_inspector(Panel& p, Pending& pending) {
    if (const auto& link = p.canvas.selected_link(); link.has_value()) {
        draw_link_inspector(p, *link, pending);
        return;
    }
    const Node* node = p.graph->find(p.canvas.selected_node());
    if (node == nullptr) {
        ImGui::TextWrapped("Select a node to read and edit its code. Drag a node to move it. Drag from a node's right dot to "
                           "another node's left dot to link them for viewing.");
        return;
    }
    if (p.draftKey != node->key) {
        p.codeDraft = std::string(p.graph->code(*node));
        p.draftKey = node->key;
    }
    draw_node_inspector(p, *node, pending);
}

/** Applies the canvas requests, then the inspector's, one edit at a time. */
void apply_pending(Panel& p, const CanvasRequest& canvas, const Pending& pending) {
    if (!canvas.connectFrom.empty() && !canvas.connectTo.empty()) {
        const bool exists = std::any_of(p.links.begin(), p.links.end(), [&canvas](const CanvasEdge& link) {
            return link.source == canvas.connectFrom && link.target == canvas.connectTo;
        });
        if (!exists) p.links.push_back({canvas.connectFrom, canvas.connectTo, "linked for viewing", true});
        set_status(exists ? "Already linked." : "Linked for viewing only. The Lua is unchanged.", false);
    }
    if (!canvas.deleteSource.empty()) {
        std::erase_if(p.links, [&canvas](const CanvasEdge& link) {
            return link.editable && link.source == canvas.deleteSource && link.target == canvas.deleteTarget;
        });
        p.canvas.clear_selection();
    }
    if (!pending.dropSource.empty()) {
        std::erase_if(p.links, [&pending](const CanvasEdge& link) {
            return link.editable && link.source == pending.dropSource && link.target == pending.dropTarget;
        });
        p.canvas.clear_selection();
    }
    if (!canvas.removeNode.empty()) apply(p.graph->without(canvas.removeNode), "Removed node.");
    if (!pending.removeKey.empty()) {
        p.canvas.clear_selection();
        apply(p.graph->without(pending.removeKey), "Removed node.");
    }
    if (!pending.applyKey.empty()) apply(p.graph->with_code(pending.applyKey, pending.applyCode), "Applied code.");
    if (!pending.moveKey.empty()) apply(p.graph->moved(pending.moveKey, pending.moveDirection), "Moved node.");
}

void draw_body(Panel& p) {
    if (p.graph == nullptr) {
        ImGui::TextDisabled("Load a Lua mission script to show its graph.");
        return;
    }
    if (!stubs::typed_api_checks_available()) ImGui::TextDisabled("API calls are not checked: the mission SDK schema is not ported.");
    draw_add(p);

    std::vector<CanvasNode> nodes;
    nodes.reserve(p.graph->nodes().size());
    for (const Node& node : p.graph->nodes()) {
        CanvasNode item;
        item.key = node.key;
        item.title = node.title;
        item.subtitle = node.subtitle;
        // Only code and module statements show their lines inside the node, as in the C# view.
        if (node.kind == NodeKind::code || node.kind == NodeKind::module) item.preview = preview_of(p.graph->code(node));
        item.tint = tint_of(node);
        nodes.push_back(std::move(item));
    }
    std::vector<CanvasEdge> edges;
    edges.reserve(p.graph->edges().size() + p.links.size());
    for (const Edge& edge : p.graph->edges()) edges.push_back({edge.source, edge.target, edge.label, false});
    edges.insert(edges.end(), p.links.begin(), p.links.end());

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float inspectorWidth = std::min(std::clamp(avail.x * 0.34F, 220.0F, 420.0F), avail.x * 0.5F);
    CanvasRequest request;
    ImGui::BeginChild("mission-canvas", ImVec2(std::max(100.0F, avail.x - inspectorWidth - 8.0F), avail.y), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    p.canvas.draw(nodes, edges, true, request);
    ImGui::EndChild();
    ImGui::SameLine();

    Pending pending;
    ImGui::BeginChild("mission-inspector", ImVec2(0.0F, avail.y), ImGuiChildFlags_Borders);
    draw_inspector(p, pending);
    ImGui::EndChild();

    if (p.canvas.take_moved()) (void)stubs::save_layout(p.path);
    apply_pending(p, request, pending);
}

void draw_panel(Panel& p) {
    draw_toolbar(p);
    if (!p.status.empty()) {
        if (p.statusFailed) {
            ImGui::TextColored(kFailColor, "%s", p.status.c_str());
        } else {
            ImGui::TextDisabled("%s", p.status.c_str());
        }
    }
    draw_body(p);
}

} // namespace

bool open_file(std::string_view path, std::string& error) noexcept {
    try {
        return load_file(std::string(path), error);
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

void draw() noexcept {
    Panel& p = state();
    try {
        draw_panel(p);
    } catch (const std::exception& failure) {
        set_status(failure.what(), true);
    }
}

} // namespace dawn::tools::studio::mission_graph
