#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

namespace dawn::tools::studio::mission_graph {

/** One box on the canvas. The preview is code shown inside the box and may be empty. */
struct CanvasNode final {
    std::string key;
    std::string title;
    std::string subtitle;
    std::string preview;
    ImU32 tint{};
};

/** A link between two nodes. Only an editable link can be removed from the canvas. */
struct CanvasEdge final {
    std::string source;
    std::string target;
    std::string label;
    bool editable{};
};

/** Edits the canvas asks its owner to make. An empty field asks for nothing. */
struct CanvasRequest final {
    std::string connectFrom;
    std::string connectTo;
    std::string deleteSource;
    std::string deleteTarget;
    std::string removeNode;
};

/**
 * A pan-and-zoom node canvas. It moves nodes, selects them and their links, and turns drags from one node's
 * output dot to another's input dot into requests. It never changes the graph; the owner applies each request
 * on the next frame, after this draw has finished using the node pointers.
 */
class Canvas final {
public:
    /** Forgets every node position, so the next draw lays the nodes out again in rows. */
    void reset_layout() noexcept;

    /** Frames every node on the next draw. */
    void frame_all() noexcept { fit_ = true; }

    void clear_selection() noexcept;

    /** True once after a node has been dragged, so the owner can save its layout. */
    [[nodiscard]] bool take_moved() noexcept {
        const bool value = moved_;
        moved_ = false;
        return value;
    }

    /**
     * Draws the canvas in the rest of the current child window and applies this frame's input.
     * @param editable False hides connect and delete.
     * @param request Receives this frame's edit requests; any earlier contents are kept.
     */
    void draw(const std::vector<CanvasNode>& nodes, const std::vector<CanvasEdge>& edges, bool editable,
              CanvasRequest& request) noexcept;

    [[nodiscard]] const std::string& selected_node() const noexcept { return selected_; }
    [[nodiscard]] const std::optional<CanvasEdge>& selected_link() const noexcept { return link_; }

private:
    void fit(const std::vector<CanvasNode>& nodes, ImVec2 size) noexcept;

    std::unordered_map<std::string, ImVec2> positions_;
    std::string selected_;
    std::optional<CanvasEdge> link_;
    std::string connectFrom_;
    std::string dragNode_;
    bool panning_{};
    bool moved_{};
    float zoom_{1.0F};
    ImVec2 pan_{30.0F, 30.0F};
    bool fit_{true};
};

} // namespace dawn::tools::studio::mission_graph
