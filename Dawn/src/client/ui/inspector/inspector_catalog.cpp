#include "inspector_catalog.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <span>

#include "inspector_stubs.h"

namespace dawn::client::ui::inspector {
namespace {

/** Trims ASCII spaces from both ends without allocating. */
[[nodiscard]] std::string_view trimmed(std::string_view text) noexcept {
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    return text;
}

/** ASCII case-insensitive substring test. An empty needle matches. */
[[nodiscard]] bool contains_text(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > haystack.size()) {
        return false;
    }
    for (std::size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
        bool same = true;
        for (std::size_t offset = 0; same && offset < needle.size(); ++offset) {
            same = std::tolower(static_cast<unsigned char>(haystack[start + offset]))
                == std::tolower(static_cast<unsigned char>(needle[offset]));
        }
        if (same) {
            return true;
        }
    }
    return false;
}

/** Collects the members a plug-pool walk visits. The visitor returns false only on allocation failure. */
struct PoolCollector {
    PlugPool* pool{};
};

bool collect_member(void* context, build::items::socket_plugs::Member member) noexcept {
    auto* collector = static_cast<PoolCollector*>(context);
    ++collector->pool->total;
    if (collector->pool->members.size() >= kListLimit) {
        return true;
    }
    try {
        collector->pool->members.push_back(member);
    } catch (...) {
        return false;
    }
    return true;
}

} // namespace

bool load(Snapshot& snapshot) noexcept {
    snapshot = Snapshot{};
    try {
        snapshot.itemsReady = build::item_definitions_ready();
        snapshot.detailsReady = build::configured_item_details_ready();
        snapshot.collectiblesReady = build::collectible_definitions_ready();
        snapshot.materialsReady = build::material_requirement_sets_ready();
        snapshot.socketsReady = build::socket_plug_rules_ready();
        snapshot.bucketsReady = build::inventory_bucket_descriptors_ready();
        snapshot.constantsReady = build::investment_constants_ready();
        if (!snapshot.itemsReady) {
            return false;
        }

        snapshot.itemRows.resize(build::items::count());
        std::size_t copied = 0;
        if (!build::items::snapshot(snapshot.itemRows, copied)) {
            snapshot = Snapshot{};
            return false;
        }
        snapshot.itemRows.resize(copied);

        snapshot.itemRowByIndex.assign(kIndexCapacity, kNoRow);
        for (std::size_t row = 0; row < copied; ++row) {
            snapshot.itemRowByIndex[snapshot.itemRows[row].definitionIndex] =
                static_cast<std::uint32_t>(row);
        }

        snapshot.detailRowByIndex.assign(kIndexCapacity, kNoRow);
        if (snapshot.detailsReady) {
            for (const auto& item : snapshot.itemRows) {
                build::items::details::Definition detail{};
                if (!build::find_configured_item_detail(item.definitionIndex, detail)) {
                    continue;
                }
                snapshot.detailRowByIndex[item.definitionIndex] =
                    static_cast<std::uint32_t>(snapshot.detailRows.size());
                snapshot.detailRows.push_back(detail);

                const std::size_t perks = (std::min)(static_cast<std::size_t>(detail.sandboxPerkCount),
                                                     detail.sandboxPerks.size());
                for (std::size_t slot = 0; slot < perks; ++slot) {
                    auto& carriers = snapshot.carriersByPerk[detail.sandboxPerks[slot]];
                    if (carriers.empty() || carriers.back() != detail.definitionIndex) {
                        carriers.push_back(detail.definitionIndex);
                    }
                }
            }
        }
        return true;
    } catch (...) {
        snapshot = Snapshot{};
        return false;
    }
}

const build::items::Definition* item_at(const Snapshot& snapshot,
                                        std::uint16_t definitionIndex) noexcept {
    if (definitionIndex >= snapshot.itemRowByIndex.size()) {
        return nullptr;
    }
    const std::uint32_t row = snapshot.itemRowByIndex[definitionIndex];
    return row == kNoRow || row >= snapshot.itemRows.size() ? nullptr : &snapshot.itemRows[row];
}

const build::items::details::Definition* detail_at(const Snapshot& snapshot,
                                                   std::uint16_t definitionIndex) noexcept {
    if (definitionIndex >= snapshot.detailRowByIndex.size()) {
        return nullptr;
    }
    const std::uint32_t row = snapshot.detailRowByIndex[definitionIndex];
    return row == kNoRow || row >= snapshot.detailRows.size() ? nullptr : &snapshot.detailRows[row];
}

const std::vector<std::uint16_t>* carriers_of(const Snapshot& snapshot,
                                              std::uint16_t perkIndex) noexcept {
    const auto found = snapshot.carriersByPerk.find(perkIndex);
    return found == snapshot.carriersByPerk.end() ? nullptr : &found->second;
}

bool matches(const Snapshot& snapshot,
             const build::items::Definition& item,
             const Filter& filter) noexcept {
    if (filter.tier >= 0 && item.tier != static_cast<std::uint8_t>(filter.tier)) {
        return false;
    }
    if (filter.requireMasterwork && !build::items::masterwork_plug(item)) {
        return false;
    }
    const auto* detail = detail_at(snapshot, item.definitionIndex);
    if (filter.requireSockets && (detail == nullptr || detail->ordinarySocketCount == 0)) {
        return false;
    }
    if (filter.requireSandboxPerks && (detail == nullptr || detail->sandboxPerkCount == 0)) {
        return false;
    }

    const std::string_view query = trimmed(std::string_view{filter.query.data()});
    if (query.empty()) {
        return true;
    }
    std::uint32_t hash{};
    if (parse_hash(query, hash)) {
        return item.definitionHash == hash;
    }
    std::uint16_t index{};
    if (parse_index(query, index)) {
        return item.definitionIndex == index;
    }
    // Text search needs item names, which Dawn build data does not carry (see inspector_stubs.h).
    return contains_text(stubs::item_name_for_hash(item.definitionHash), query);
}

bool search(const Snapshot& snapshot, const Filter& filter, std::vector<std::uint32_t>& rows) noexcept {
    rows.clear();
    try {
        for (std::size_t row = 0; row < snapshot.itemRows.size(); ++row) {
            if (matches(snapshot, snapshot.itemRows[row], filter)) {
                rows.push_back(static_cast<std::uint32_t>(row));
            }
        }
        return true;
    } catch (...) {
        rows.clear();
        return false;
    }
}

PlugPool plug_pool(std::uint16_t itemDefinitionIndex, std::uint8_t lane, bool randomized) noexcept {
    PlugPool pool{};
    try {
        PoolCollector collector{&pool};
        pool.complete = randomized
            ? build::visit_socket_roll_pool(itemDefinitionIndex, lane, &collect_member, &collector)
            : build::visit_socket_plug_pool(itemDefinitionIndex, lane, &collect_member, &collector);
    } catch (...) {
        pool = PlugPool{};
    }
    return pool;
}

Acquisition acquisition_of(const Snapshot& snapshot, std::uint16_t itemDefinitionIndex) noexcept {
    Acquisition result{};
    if (!snapshot.collectiblesReady) {
        return result;
    }
    std::uint16_t collectibleIndex{};
    if (!build::collectibles::find_granting(itemDefinitionIndex, collectibleIndex)) {
        return result;
    }
    result.collectibleIndex = collectibleIndex;
    result.granted = build::find_collectible_definition(collectibleIndex, result.collectible);
    return result;
}

bool material_set(std::uint16_t requirementSetIndex,
                  build::material_requirements::Definition& definition) noexcept {
    return build::material_requirement_sets_ready()
        && build::find_material_requirement_set(requirementSetIndex, definition);
}

bool investment(build::constants::InvestmentConstants& constants) noexcept {
    return build::find_investment_constants(constants);
}

bool bucket(std::uint8_t bucketId, build::inventory::buckets::Descriptor& descriptor) noexcept {
    return build::inventory_bucket_descriptors_ready()
        && build::find_inventory_bucket_descriptor(bucketId, descriptor);
}

const char* tier_label(std::uint8_t tier) noexcept {
    switch (static_cast<build::items::Tier>(tier)) {
    case build::items::Tier::common: return "Common";
    case build::items::Tier::uncommon: return "Uncommon";
    case build::items::Tier::rare: return "Rare";
    case build::items::Tier::legendary: return "Legendary";
    case build::items::Tier::exotic: return "Exotic";
    case build::items::Tier::none: return "Untiered";
    }
    return "Unknown tier";
}

const char* array_label(build::inventory::buckets::ArraySelector selector) noexcept {
    switch (selector) {
    case build::inventory::buckets::ArraySelector::character: return "Character inventory";
    case build::inventory::buckets::ArraySelector::profile: return "Profile inventory";
    case build::inventory::buckets::ArraySelector::smallProfile: return "Small profile inventory";
    }
    return "Unknown array";
}

bool parse_hash(std::string_view text, std::uint32_t& value) noexcept {
    if (text.size() < 3 || text.size() > 10 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X')) {
        return false;
    }
    std::uint64_t result = 0;
    for (const char character : text.substr(2)) {
        std::uint64_t digit{};
        if (character >= '0' && character <= '9') {
            digit = static_cast<std::uint64_t>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = static_cast<std::uint64_t>(character - 'a' + 10);
        } else if (character >= 'A' && character <= 'F') {
            digit = static_cast<std::uint64_t>(character - 'A' + 10);
        } else {
            return false;
        }
        result = result * 16U + digit;
    }
    if (result > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }
    value = static_cast<std::uint32_t>(result);
    return true;
}

bool parse_index(std::string_view text, std::uint16_t& value) noexcept {
    if (text.empty() || text.size() > 5) {
        return false;
    }
    std::uint32_t result = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        result = result * 10U + static_cast<std::uint32_t>(character - '0');
    }
    if (result > (std::numeric_limits<std::uint16_t>::max)()) {
        return false;
    }
    value = static_cast<std::uint16_t>(result);
    return true;
}

} // namespace dawn::client::ui::inspector
