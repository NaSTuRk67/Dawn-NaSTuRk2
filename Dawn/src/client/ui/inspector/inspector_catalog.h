#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

#include "../../../state/build_data/inventory/buckets/definition.h"
#include "../../../state/build_data/items/details/definition.h"
#include "../../../state/build_data/items/item_catalog.h"
#include "../../../state/build_data/runtime.h"

namespace dawn::client::ui::inspector {

namespace build = ::dawn::state::build_data;

/** Native item-definition indices are 16 bits, so a dense lookup table covers every one. */
inline constexpr std::size_t kIndexCapacity = 65536;
/** Row value meaning no row is published for a native index. */
inline constexpr std::uint32_t kNoRow = 0xFFFFFFFFU;
/** Plug members and carrier items listed under one header before the list says it was cut. */
inline constexpr std::size_t kListLimit = 64;
/** Filter text capacity in bytes, including the terminating null. */
inline constexpr std::size_t kQueryCapacity = 96;

/**
 * Everything the module reads from State, copied once and kept until the user reloads.
 * Each *Ready flag records whether that build-data domain was published when the copy was made.
 */
struct Snapshot {
    bool itemsReady{};
    bool detailsReady{};
    bool collectiblesReady{};
    bool materialsReady{};
    bool socketsReady{};
    bool bucketsReady{};
    bool constantsReady{};
    /** Item rows in native definition-index order. */
    std::vector<build::items::Definition> itemRows{};
    /** Configured item details in the order their items were visited. */
    std::vector<build::items::details::Definition> detailRows{};
    /** Row in itemRows for each native item index, or kNoRow. */
    std::vector<std::uint32_t> itemRowByIndex{};
    /** Row in detailRows for each native item index, or kNoRow. */
    std::vector<std::uint32_t> detailRowByIndex{};
    /** Native item indices whose configured detail declares each sandbox perk index, ascending. */
    std::map<std::uint16_t, std::vector<std::uint16_t>> carriersByPerk{};
};

/**
 * Copies the item, detail, and carrier tables out of State.
 * @param snapshot Receives the copy. Left empty when the item table is not ready.
 * @return True when the item table is ready and copied.
 */
[[nodiscard]] bool load(Snapshot& snapshot) noexcept;

/** @return The item row for one native index, or nullptr when the table lacks it. */
[[nodiscard]] const build::items::Definition* item_at(const Snapshot& snapshot,
                                                      std::uint16_t definitionIndex) noexcept;

/** @return The configured detail for one native index, or nullptr when it has none. */
[[nodiscard]] const build::items::details::Definition* detail_at(const Snapshot& snapshot,
                                                                 std::uint16_t definitionIndex) noexcept;

/** @return Native item indices that carry one sandbox perk, or nullptr when none do. */
[[nodiscard]] const std::vector<std::uint16_t>* carriers_of(const Snapshot& snapshot,
                                                            std::uint16_t perkIndex) noexcept;

/** Browse and search criteria. Equality decides whether the result list must be rebuilt. */
struct Filter {
    /** Search text: "0x" plus hex is a definition hash, digits alone are a native index. */
    std::array<char, kQueryCapacity> query{};
    /** -1 for any tier, otherwise the native tier value 1 through 5. */
    int tier{-1};
    /** Only items whose configured detail declares ordinary sockets. */
    bool requireSockets{};
    /** Only masterwork plug definitions at the top tier. */
    bool requireMasterwork{};
    /** Only items whose configured detail declares sandbox perks. */
    bool requireSandboxPerks{};

    [[nodiscard]] bool operator==(const Filter&) const = default;
};

/** @return True when one item row passes every criterion in the filter. */
[[nodiscard]] bool matches(const Snapshot& snapshot,
                           const build::items::Definition& item,
                           const Filter& filter) noexcept;

/**
 * Fills rows with the item-row numbers that pass the filter, in native order.
 * @return False when the result storage could not be built.
 */
[[nodiscard]] bool search(const Snapshot& snapshot,
                          const Filter& filter,
                          std::vector<std::uint32_t>& rows) noexcept;

/** The plug members one socket lane accepts, or the members of its randomized roll pool. */
struct PlugPool {
    /** At most kListLimit members, in the native order State stores them. */
    std::vector<std::uint16_t> members{};
    /** Every member, including those past kListLimit. */
    std::size_t total{};
    /** False when the relation is missing or could not be walked. */
    bool complete{};
};

/** @return The plug pool for one ordinary socket lane of one item. */
[[nodiscard]] PlugPool plug_pool(std::uint16_t itemDefinitionIndex,
                                 std::uint8_t lane,
                                 bool randomized) noexcept;

/** Whether a Collections entry grants the item, and the entry that does. */
struct Acquisition {
    bool granted{};
    std::uint16_t collectibleIndex{};
    build::collectibles::Definition collectible{};
};

/** @return The Collections entry that grants one item, if any. */
[[nodiscard]] Acquisition acquisition_of(const Snapshot& snapshot,
                                         std::uint16_t itemDefinitionIndex) noexcept;

/** @return True when the material-requirement set is published and was read into definition. */
[[nodiscard]] bool material_set(std::uint16_t requirementSetIndex,
                                build::material_requirements::Definition& definition) noexcept;

/** @return True when the installed investment constants are published and were read. */
[[nodiscard]] bool investment(build::constants::InvestmentConstants& constants) noexcept;

/** @return True when the bucket descriptor is published and was read. */
[[nodiscard]] bool bucket(std::uint8_t bucketId,
                          build::inventory::buckets::Descriptor& descriptor) noexcept;

/** @return The display label for a native tier value. */
[[nodiscard]] const char* tier_label(std::uint8_t tier) noexcept;

/** @return The display label for the native inventory array a bucket draws slots from. */
[[nodiscard]] const char* array_label(build::inventory::buckets::ArraySelector selector) noexcept;

/** Parses "0x" followed by up to eight hex digits. */
[[nodiscard]] bool parse_hash(std::string_view text, std::uint32_t& value) noexcept;

/** Parses up to five decimal digits that fit a native item index. */
[[nodiscard]] bool parse_index(std::string_view text, std::uint16_t& value) noexcept;

} // namespace dawn::client::ui::inspector
