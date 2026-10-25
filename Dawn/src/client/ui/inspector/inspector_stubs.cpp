#include "inspector_stubs.h"

namespace dawn::client::ui::inspector::stubs {

/** STUB: see inspector_stubs.h. Always empty in this port. */
std::string_view item_name_for_hash([[maybe_unused]] std::uint32_t definitionHash) noexcept {
    return {};
}

/** STUB: see inspector_stubs.h. Always empty in this port. */
std::string_view perk_name([[maybe_unused]] std::uint16_t perkIndex) noexcept {
    return {};
}

/** STUB: see inspector_stubs.h. Always empty in this port. */
std::string_view stat_name([[maybe_unused]] std::uint8_t statRow) noexcept {
    return {};
}

} // namespace dawn::client::ui::inspector::stubs
