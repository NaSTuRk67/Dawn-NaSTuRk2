#pragma once

#include <string>
#include <string_view>

namespace dawn::tools::studio::mission_graph {

/**
 * Reads a Lua mission script into the graph view. Only .lua files are read, up to 64 MiB, and the text is
 * never executed.
 * @param path Script file to read.
 * @param error Set to the reason when false is returned.
 * @return True when the script was read and split into statements.
 */
[[nodiscard]] bool open_file(std::string_view path, std::string& error) noexcept;

/** Draws the mission graph inside the active Dear ImGui window. */
void draw() noexcept;

} // namespace dawn::tools::studio::mission_graph
