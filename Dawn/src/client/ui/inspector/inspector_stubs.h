#pragma once

#include <cstdint>
#include <string_view>

namespace dawn::client::ui::inspector::stubs {

/*
 * STUBS. Dawn's build data stores item, perk and stat rows as numbers only. The Rust catalog it
 * was ported from resolves display names from a manifest that Dawn does not carry. Each stub
 * below returns an empty view, so callers fall back to the numeric identity. Replace a stub body
 * when a real name source exists; do not add names to the callers.
 */

/** STUB: no item display-name table. Empty until one is wired. */
[[nodiscard]] std::string_view item_name_for_hash(std::uint32_t definitionHash) noexcept;

/** STUB: no sandbox perk name table. */
[[nodiscard]] std::string_view perk_name(std::uint16_t perkIndex) noexcept;

/** STUB: no stat name table. Stat rows are shown by their row number. */
[[nodiscard]] std::string_view stat_name(std::uint8_t statRow) noexcept;

} // namespace dawn::client::ui::inspector::stubs
