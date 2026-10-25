/** The node canvas: layout, drawing and input. Positions are in canvas units; zoom 1 draws them at 100%. */

#include "mission_graph_canvas.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <unordered_set>

#include <imgui.h>

namespace dawn::tools::studio::mission_graph {
namespace {

constexpr float kNodeWidth = 260.0F;
constexpr float kHeader = 44.0F;
constexpr float kCodeLine = 15.0F;
constexpr float kColumnGap = 300.0F;
constexpr float kRowGap = 200.0F;
constexpr std::size_t kMaxPreviewLines = 7;
constexpr float kMinZoom = 0.25F;
constexpr float kMaxZoom = 2.0F;
constexpr float kEdgeHit = 6.0F;
constexpr int kEdgeSamples = 24;

constexpr ImU32 kBackground = IM_COL32(18, 20, 24, 255);
constexpr ImU32 kNodeFill = IM_COL32(36, 40, 48, 255);
constexpr ImU32 kNodeSelected = IM_COL32(46, 51, 62, 255);
constexpr ImU32 kBorder = IM_COL32(70, 77, 90, 255);
constexpr ImU32 kAccent = IM_COL32(96, 163, 255, 255);
constexpr ImU32 kPort = IM_COL32(158, 184, 219, 255);
constexpr ImU32 kFixedLink = IM_COL32(92, 110, 138, 255);
constexpr ImU32 kHighlight = IM_COL32(255, 217, 166, 255);

ImVec2 add(ImVec2 a, ImVec2 b) noexcept { return ImVec2(a.x + b.x, a.y + b.y); }
ImVec2 sub(ImVec2 a, ImVec2 b) noexcept { return ImVec2(a.x - b.x, a.y - b.y); }
ImVec2 scale(ImVec2 a, float k) noexcept { return ImVec2(a.x * k, a.y * k); }
float distance(ImVec2 a, ImVec2 b) noexcept { return std::hypot(a.x - b.x, a.y - b.y); }

/** The output dot sits at the right edge of the header; the input dot at the left edge. */
ImVec2 in_port(ImVec2 min, float zoom) noexcept { return ImVec2(min.x, min.y + kHeader * 0.5F * zoom); }
ImVec2 out_port(ImVec2 min, float zoom) noexcept { return ImVec2(min.x + kNodeWidth * zoom, min.y + kHeader * 0.5F * zoom); }

/** A link drawn as a cubic curve from an output dot to an input dot. */
struct Curve final {
    ImVec2 a, c1, c2, b;
};

Curve curve(ImVec2 from, ImVec2 to, float zoom) noexcept {
    const float reach = std::max(60.0F, std::fabs(to.x - from.x) * 0.5F) * zoom;
    return {from, ImVec2(from.x + reach, from.y), ImVec2(to.x - reach, to.y), to};
}

ImVec2 bezier(const Curve& c, float t) noexcept {
    const float u = 1.0F - t;
    const float w0 = u * u * u, w1 = 3.0F * u * u * t, w2 = 3.0F * u * t * t, w3 = t * t * t;
    return ImVec2(w0 * c.a.x + w1 * c.c1.x + w2 * c.c2.x + w3 * c.b.x, w0 * c.a.y + w1 * c.c1.y + w2 * c.c2.y + w3 * c.b.y);
}

float distance_to_segment(ImVec2 p, ImVec2 a, ImVec2 b) noexcept {
    const ImVec2 ab = sub(b, a);
    const float length = ab.x * ab.x + ab.y * ab.y;
    const float t = length > 0.0F ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / length, 0.0F, 1.0F) : 0.0F;
    return distance(p, add(a, scale(ab, t)));
}

/** The distance from p to the sampled curve. */
float distance_to_curve(const Curve& c, ImVec2 p) noexcept {
    float best = FLT_MAX;
    ImVec2 previous = c.a;
    for (int i = 1; i <= kEdgeSamples; ++i) {
        const ImVec2 current = bezier(c, static_cast<float>(i) / static_cast<float>(kEdgeSamples));
        best = std::min(best, distance_to_segment(p, previous, current));
        previous = current;
    }
    return best;
}

/** The code preview as lines; a trailing newline does not start an extra line. */
std::vector<std::string_view> preview_lines(std::string_view preview) {
    std::vector<std::string_view> lines;
    std::size_t begin = 0;
    while (begin < preview.size()) {
        std::size_t end = preview.find('\n', begin);
        if (end == std::string_view::npos) end = preview.size();
        lines.push_back(preview.substr(begin, end - begin));
        begin = end + 1;
    }
    return lines;
}

/** Height in canvas units: the header, then up to a few preview lines, with "..." when more are cut. */
float node_height(const CanvasNode& node) {
    const std::size_t count = preview_lines(node.preview).size();
    if (count == 0) return kHeader;
    const std::size_t shown = std::min(count, kMaxPreviewLines) + (count > kMaxPreviewLines ? 1 : 0);
    return kHeader + 8.0F + static_cast<float>(shown) * kCodeLine;
}

ImVec2 node_size(const CanvasNode& node) { return ImVec2(kNodeWidth, node_height(node)); }

/** One node: header with title and subtitle, the preview lines, then its two dots. */
void draw_node(ImDrawList* list, const CanvasNode& node, ImVec2 min, float zoom, bool selected) {
    const ImVec2 max = add(min, scale(node_size(node), zoom));
    const float rounding = 5.0F * zoom;
    list->AddRectFilled(min, max, selected ? kNodeSelected : kNodeFill, rounding);
    list->AddRectFilled(min, ImVec2(max.x, min.y + kHeader * zoom), node.tint, rounding, ImDrawFlags_RoundCornersTop);
    list->AddRect(min, max, selected ? kAccent : kBorder, rounding, ImDrawFlags_None, selected ? 2.0F : 1.0F);

    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    list->PushClipRect(add(min, ImVec2(6.0F * zoom, 0.0F)), ImVec2(max.x - 6.0F * zoom, max.y), true);
    list->AddText(font, fontSize * zoom, add(min, scale(ImVec2(14.0F, 7.0F), zoom)), ImGui::GetColorU32(ImGuiCol_Text),
                  node.title.c_str());
    list->AddText(font, fontSize * 0.8F * zoom, add(min, scale(ImVec2(14.0F, 27.0F), zoom)),
                  ImGui::GetColorU32(ImGuiCol_TextDisabled), node.subtitle.c_str());
    const auto lines = preview_lines(node.preview);
    float y = kHeader + 8.0F;
    for (std::size_t i = 0; i < lines.size() && i < kMaxPreviewLines; ++i) {
        const std::string_view line = lines[i];
        list->AddText(font, fontSize * 0.85F * zoom, add(min, scale(ImVec2(14.0F, y), zoom)), ImGui::GetColorU32(ImGuiCol_Text),
                      line.data(), line.data() + line.size());
        y += kCodeLine;
    }
    if (lines.size() > kMaxPreviewLines) {
        list->AddText(font, fontSize * 0.85F * zoom, add(min, scale(ImVec2(14.0F, y), zoom)),
                      ImGui::GetColorU32(ImGuiCol_TextDisabled), "...");
    }
    list->PopClipRect();

    const float dot = std::max(4.0F, 6.0F * zoom);
    list->AddCircleFilled(in_port(min, zoom), dot, kPort);
    list->AddCircleFilled(out_port(min, zoom), dot, kPort);
}

} // namespace

void Canvas::reset_layout() noexcept {
    positions_.clear();
    fit_ = true;
}

void Canvas::clear_selection() noexcept {
    selected_.clear();
    link_.reset();
}

void Canvas::fit(const std::vector<CanvasNode>& nodes, ImVec2 size) noexcept {
    ImVec2 low(FLT_MAX, FLT_MAX), high(-FLT_MAX, -FLT_MAX);
    bool any = false;
    for (const auto& node : nodes) {
        const auto found = positions_.find(node.key);
        if (found == positions_.end()) continue;
        const ImVec2 p = found->second;
        const ImVec2 q = add(p, node_size(node));
        low = ImVec2(std::min(low.x, p.x), std::min(low.y, p.y));
        high = ImVec2(std::max(high.x, q.x), std::max(high.y, q.y));
        any = true;
    }
    if (!any) {
        zoom_ = 1.0F;
        pan_ = ImVec2(30.0F, 30.0F);
        return;
    }
    const ImVec2 extent(std::max(high.x - low.x, 1.0F), std::max(high.y - low.y, 1.0F));
    const ImVec2 center((low.x + high.x) * 0.5F, (low.y + high.y) * 0.5F);
    zoom_ = std::clamp(std::min((size.x - 60.0F) / extent.x, (size.y - 60.0F) / extent.y), 0.25F, 1.0F);
    pan_ = ImVec2(size.x * 0.5F - center.x * zoom_, size.y * 0.5F - center.y * zoom_);
}

void Canvas::draw(const std::vector<CanvasNode>& nodes, const std::vector<CanvasEdge>& edges, bool editable,
                  CanvasRequest& request) noexcept {
    // Positions of nodes that are gone are dropped; a new node takes the next grid cell.
    std::unordered_set<std::string> keys;
    for (const auto& node : nodes) keys.insert(node.key);
    std::erase_if(positions_, [&keys](const auto& entry) { return !keys.contains(entry.first); });
    for (const auto& node : nodes) {
        if (!positions_.contains(node.key)) {
            const std::size_t slot = positions_.size();
            positions_[node.key] = ImVec2(static_cast<float>(slot % 4) * kColumnGap, static_cast<float>(slot / 4) * kRowGap);
        }
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 size(std::max(available.x, 50.0F), std::max(available.y, 50.0F));
    ImGui::InvisibleButton("##mission_graph_canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    if (fit_) {
        fit(nodes, size);
        fit_ = false;
    }

    // Zoom keeps the point under the pointer where it is.
    if (hovered && io.MouseWheel != 0.0F) {
        const float next = std::clamp(zoom_ * std::pow(1.12F, io.MouseWheel), kMinZoom, kMaxZoom);
        const ImVec2 anchor = sub(io.MousePos, origin);
        pan_ = sub(anchor, scale(sub(anchor, pan_), next / zoom_));
        zoom_ = next;
    }

    const auto screen = [&](ImVec2 at) { return add(origin, add(pan_, scale(at, zoom_))); };
    const auto place = [this](const std::string& key) { return positions_.at(key); };
    const auto linked = [this](const CanvasEdge& edge) { return positions_.contains(edge.source) && positions_.contains(edge.target); };

    // Hit test with this frame's positions. The topmost node is the last one drawn, so it wins.
    const float hitRadius = std::max(4.0F, 7.0F * zoom_) * 1.8F;
    const ImVec2 mouse = io.MousePos;
    const CanvasNode* hitNode = nullptr;
    const CanvasNode* hitIn = nullptr;
    const CanvasNode* hitOut = nullptr;
    const CanvasEdge* hitEdge = nullptr;
    if (hovered) {
        for (const auto& node : nodes) {
            const ImVec2 min = screen(place(node.key));
            const ImVec2 max = add(min, scale(node_size(node), zoom_));
            if (mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y) hitNode = &node;
            if (distance(mouse, in_port(min, zoom_)) <= hitRadius) hitIn = &node;
            if (distance(mouse, out_port(min, zoom_)) <= hitRadius) hitOut = &node;
        }
        if (hitNode == nullptr && hitIn == nullptr && hitOut == nullptr) {
            for (const auto& edge : edges) {
                if (!linked(edge)) continue;
                const Curve c = curve(out_port(screen(place(edge.source)), zoom_), in_port(screen(place(edge.target)), zoom_), zoom_);
                if (distance_to_curve(c, mouse) < kEdgeHit) hitEdge = &edge;
            }
        }
    }

    // Clicks: an output dot starts a link, a node is selected and can be moved, a link is selected, or the
    // background is dragged to pan.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (editable && hitOut != nullptr) {
            connectFrom_ = hitOut->key;
        } else if (hitNode != nullptr) {
            selected_ = hitNode->key;
            link_.reset();
            dragNode_ = hitNode->key;
        } else if (hitEdge != nullptr) {
            link_ = *hitEdge;
            selected_.clear();
        } else {
            clear_selection();
            panning_ = true;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        dragNode_.clear();
        panning_ = false;
    }
    if (!dragNode_.empty() && positions_.contains(dragNode_) && zoom_ > 0.0F) {
        ImVec2& at = positions_[dragNode_];
        at.x += io.MouseDelta.x / zoom_;
        at.y += io.MouseDelta.y / zoom_;
        if (io.MouseDelta.x != 0.0F || io.MouseDelta.y != 0.0F) moved_ = true;
    }
    if (panning_ || (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
        pan_.x += io.MouseDelta.x;
        pan_.y += io.MouseDelta.y;
    }

    // A link is made when the pointer is released over another node's input dot, or anywhere on that node.
    if (!connectFrom_.empty() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const CanvasNode* target = hitIn != nullptr ? hitIn : hitNode;
        if (target != nullptr && target->key != connectFrom_) {
            request.connectFrom = connectFrom_;
            request.connectTo = target->key;
        }
        connectFrom_.clear();
    }

    // Delete removes the selected link if it is editable, otherwise the selected node.
    if (hovered && editable && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (link_.has_value() && link_->editable) {
            request.deleteSource = link_->source;
            request.deleteTarget = link_->target;
            link_.reset();
        } else if (!selected_.empty()) {
            request.removeNode = selected_;
            selected_.clear();
        }
    }

    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 end = add(origin, size);
    list->PushClipRect(origin, end, true);
    list->AddRectFilled(origin, end, kBackground);

    // Links of the selected node are drawn last, so they sit on top of the rest.
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& edge : edges) {
            if (!linked(edge)) continue;
            const bool touches = !selected_.empty() && (edge.source == selected_ || edge.target == selected_);
            if (touches != (pass == 1)) continue;
            const bool picked = link_.has_value() && link_->source == edge.source && link_->target == edge.target;
            const ImU32 color = picked ? kHighlight : touches ? kPort : edge.editable ? kAccent : kFixedLink;
            const Curve c = curve(out_port(screen(place(edge.source)), zoom_), in_port(screen(place(edge.target)), zoom_), zoom_);
            list->AddBezierCubic(c.a, c.c1, c.c2, c.b, color, (picked ? 3.5F : touches ? 3.0F : 2.0F) * std::max(0.5F, zoom_));
            if (touches && !edge.label.empty()) {
                const ImVec2 mid = bezier(c, 0.5F);
                const float fontSize = ImGui::GetFontSize() * std::clamp(zoom_, 0.75F, 1.0F);
                const ImVec2 text = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0F, edge.label.c_str());
                const ImVec2 at(mid.x - text.x * 0.5F, mid.y - text.y * 0.5F);
                list->AddRectFilled(ImVec2(at.x - 3.0F, at.y - 1.0F), ImVec2(at.x + text.x + 3.0F, at.y + text.y + 1.0F), kBackground, 2.0F);
                list->AddText(ImGui::GetFont(), fontSize, at, color, edge.label.c_str());
            }
        }
    }

    for (const auto& node : nodes) {
        list->PushClipRect(origin, end, true);
        draw_node(list, node, screen(place(node.key)), zoom_, node.key == selected_);
        list->PopClipRect();
    }

    // The link being dragged follows the pointer.
    if (!connectFrom_.empty() && positions_.contains(connectFrom_)) {
        const Curve c = curve(out_port(screen(place(connectFrom_)), zoom_), mouse, zoom_);
        list->AddBezierCubic(c.a, c.c1, c.c2, c.b, kHighlight, 2.0F * std::max(0.5F, zoom_));
    }
    list->PopClipRect();

    if (hitEdge != nullptr && !hitEdge->label.empty()) ImGui::SetTooltip("%s", hitEdge->label.c_str());
}

} // namespace dawn::tools::studio::mission_graph
