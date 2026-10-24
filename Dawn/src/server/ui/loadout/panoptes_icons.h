#pragma once

#include <cstdint>
#include <string_view>
#include <imgui.h>

namespace dawn::server::ui::loadout::icons {

/** Returns the lazily decoded Panoptes icon for a definition hash. */
[[nodiscard]] ImTextureID texture(std::uint32_t hash) noexcept;

/** Returns the Panoptes display name for a definition hash, or an empty view. */
[[nodiscard]] std::string_view name(std::uint32_t hash) noexcept;

/** Releases every cached GPU texture. */
void shutdown() noexcept;

} // namespace dawn::server::ui::loadout::icons
