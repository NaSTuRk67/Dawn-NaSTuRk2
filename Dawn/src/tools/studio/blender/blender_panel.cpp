/** Blender interchange panel. Export and Import run at once when pressed, and the last outcome stays on screen. */

#include "blender_panel.h"

#include <array>
#include <cstdio>
#include <imgui.h>

#include "blender_interchange.h"
#include "blender_scene_bridge.h"

namespace dawn::tools::studio::blender::panel {
namespace {

constexpr const char* kDefaultPath = "dawn_studio_scene.dsi";

/** Copies a result message into a fixed buffer for display. */
void set_status(std::array<char, 320>& status, const interchange::Result& result, const char* success) {
    if (result.ok()) {
        (void)std::snprintf(status.data(), status.size(), "%s", success);
    } else {
        (void)std::snprintf(status.data(), status.size(), "%s", result.message.c_str());
    }
}

} // namespace

/** Draws the Blender interchange panel inside the active Core UI frame. */
void draw() noexcept {
    static std::array<char, 260> path{};
    static std::array<char, 320> status{};
    static bool pathLoaded{};
    if (!pathLoaded) {
        (void)std::snprintf(path.data(), path.size(), "%s", kDefaultPath);
        pathLoaded = true;
    }

    ImGui::TextUnformatted("Blender Interchange");
    ImGui::Separator();
    ImGui::TextWrapped("Exports and imports mesh vertices, indices and object transforms in a .dsi file.");
    ImGui::SetNextItemWidth(360.0F);
    (void)ImGui::InputText("##blender_interchange_path", path.data(), path.size());

    if (ImGui::Button("Export")) {
        interchange::Document document;
        const interchange::Result collected = bridge::collect_scene_objects(document);
        if (!collected.ok()) {
            set_status(status, collected, "");
        } else {
            const interchange::Result written = interchange::write_file(path.data(), document);
            set_status(status, written, "Export complete.");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Import")) {
        interchange::Document document;
        const interchange::Result read = interchange::read_file(path.data(), document);
        if (!read.ok()) {
            set_status(status, read, "");
        } else {
            const interchange::Result applied = bridge::apply_scene_objects(document);
            set_status(status, applied, "Import complete.");
        }
    }
    if (status[0] != '\0') {
        ImGui::TextWrapped("%s", status.data());
    }
    ImGui::Spacing();
}

} // namespace dawn::tools::studio::blender::panel
