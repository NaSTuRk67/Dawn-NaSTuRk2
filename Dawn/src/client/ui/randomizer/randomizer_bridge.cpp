// NOT COMPILED OR TESTED. Every body is a stub. None of them changes an account or reads one.

#include "randomizer_bridge.h"

namespace dawn::client::ui::randomizer::bridge {
namespace {

/** Text every stub reports, so the panel shows the same reason everywhere. */
constexpr const char* kNotConnected = "Loadout tools are not connected in this client build.";

} // namespace

/** STUB. Always false. */
bool is_connected() noexcept {
    return false;
}

/** STUB. Changes nothing; reports that no editor session is attached. */
Outcome randomize_loadout(const LoadoutOptions& options, int itemPower) noexcept {
    (void)options;
    (void)itemPower;
    return Outcome{false, kNotConnected};
}

/** STUB. Returns zero totals. */
Stats equipped_totals() noexcept {
    return Stats{};
}

/** STUB. Reports an unreadable preview, so the panel disables Adjust Armor. */
ArmorPreview preview_armor(const Stats& targets, bool allowInventorySwaps) noexcept {
    (void)targets;
    (void)allowInventorySwaps;
    return ArmorPreview{};
}

/** STUB. Changes nothing; reports that no editor session is attached. */
Outcome adjust_armor(const Stats& targets, bool allowInventorySwaps) noexcept {
    (void)targets;
    (void)allowInventorySwaps;
    return Outcome{false, kNotConnected};
}

} // namespace dawn::client::ui::randomizer::bridge
