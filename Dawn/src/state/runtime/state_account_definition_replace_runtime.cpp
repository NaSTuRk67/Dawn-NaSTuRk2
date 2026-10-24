#include "runtime.h"

#include "../../middleware/datagen/family4/loadout/loadout_resolver.h"
#include "../build_data/runtime.h"
#include "state_account_transaction_helpers.h"
#include "storage/internal.h"

namespace dawn::state {

/** Atomically replaces one selected-character item definition after full loadout validation. */
bool replace_item_definition(std::uint64_t instanceSoid, std::uint32_t definitionHash) noexcept {
    namespace family4_loadout = middleware::datagen::family4::loadout;
    namespace detail = runtime::detail;
    if (instanceSoid == 0 || definitionHash == account::inventory::kNoDefinitionHash) {
        return false;
    }

    build_data::items::Definition replacement{};
    build_data::items::details::Definition replacementDetail{};
    if (!build_data::find_item_definition_hash(definitionHash, replacement)
        || replacement.bucketId == build_data::items::kUnresolvedBucketId
        || !build_data::find_configured_item_detail(replacement.definitionIndex, replacementDetail)
        || replacementDetail.definitionHash != definitionHash
        || replacementDetail.bucketId != replacement.bucketId) {
        return false;
    }

    account::inventory::Sockets replacementSockets{};
    replacementSockets.policy = account::inventory::SocketPolicy::authored;
    replacementSockets.plugCount = replacementDetail.ordinarySocketCount;
    if (replacementSockets.plugCount > replacementSockets.plugs.size()) {
        return false;
    }
    for (std::size_t lane = 0; lane < replacementSockets.plugCount; ++lane) {
        const std::uint16_t plugIndex = replacementDetail.initialPlugIndices[lane];
        if (plugIndex == build_data::items::details::kUnavailableItemIndex) {
            replacementSockets.plugs[lane].reset();
            continue;
        }
        build_data::items::Definition plug{};
        if (!build_data::find_item_definition_index(plugIndex, plug)) {
            return false;
        }
        replacementSockets.plugs[lane] = plug.definitionHash;
    }

    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    AccountState candidate = runtime::storage::g_state.account;
    std::size_t characterIndex = candidate.characterCount;
    for (std::size_t index = 0; index < candidate.characterCount; ++index) {
        if (candidate.characters[index].selected) {
            characterIndex = index;
            break;
        }
    }
    if (characterIndex == candidate.characterCount) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }

    CharacterState& character = candidate.characters[characterIndex];
    detail::CharacterItemLocation location{};
    account::inventory::Item* item = nullptr;
    build_data::items::Definition previous{};
    if (!detail::find_character_item_location(character, instanceSoid, location)
        || (item = detail::character_item_at(character, location)) == nullptr
        || !build_data::find_item_definition_hash(item->definitionHash, previous)
        || previous.bucketId == build_data::items::kUnresolvedBucketId
        || replacement.bucketId != previous.bucketId) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }

    item->definitionHash = definitionHash;
    item->sockets = replacementSockets;
    if (location.equipped
        && location.index
               == static_cast<std::size_t>(account::inventory::EquipmentSlot::subclass)) {
        character.movementAbilityEntry = kDefaultMovementAbilityEntry;
        character.grenadeAbilityEntry = kDefaultGrenadeAbilityEntry;
        character.superAbilityEntry = kDefaultSuperAbilityEntry;
        character.meleeAbilityEntry = kDefaultMeleeAbilityEntry;
        character.classAbilityEntry = kDefaultClassAbilityEntry;
    }
    family4_loadout::ResolvedLoadout resolved{};
    detail::ResolvedPosition position{};
    if (!account::valid(candidate) || !family4_loadout::resolve(candidate, characterIndex, resolved)
        || !detail::find_resolved_position(resolved, instanceSoid, position)
        || position.equipped != location.equipped) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }

    runtime::storage::g_state.account = candidate;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return true;
}

bool set_subclass_attunement(std::uint8_t superEntry, std::uint8_t meleeEntry) noexcept {
    const AccountState snapshot = account_snapshot();
    for (std::size_t index = 0; index < snapshot.characterCount; ++index) {
        const CharacterState& character = snapshot.characters[index];
        if (character.selected) {
            return set_subclass_abilities(character.movementAbilityEntry,
                                          character.grenadeAbilityEntry,
                                          superEntry,
                                          meleeEntry,
                                          character.classAbilityEntry);
        }
    }
    return false;
}

bool set_subclass_abilities(std::uint8_t movementEntry,
                            std::uint8_t grenadeEntry,
                            std::uint8_t superEntry,
                            std::uint8_t meleeEntry,
                            std::uint8_t classEntry) noexcept {
    namespace family4_loadout = middleware::datagen::family4::loadout;
    constexpr std::size_t subclassSlot =
        static_cast<std::size_t>(account::inventory::EquipmentSlot::subclass);

    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    AccountState candidate = runtime::storage::g_state.account;
    std::size_t characterIndex = candidate.characterCount;
    for (std::size_t index = 0; index < candidate.characterCount; ++index) {
        if (candidate.characters[index].selected) {
            characterIndex = index;
            break;
        }
    }
    if (characterIndex >= candidate.characterCount) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }

    CharacterState& character = candidate.characters[characterIndex];
    const auto& subclass = character.equipment.slots[subclassSlot];
    build_data::items::Definition item{};
    build_data::items::details::Definition detail{};
    if (!subclass.has_value()
        || !build_data::find_item_definition_hash(subclass->definitionHash, item)
        || !build_data::find_configured_item_detail(item.definitionIndex, detail)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }

    build_data::abilities::Selection selection{movementEntry,
                                                grenadeEntry,
                                                superEntry,
                                                meleeEntry,
                                                classEntry};
    build_data::abilities::Definition buckets{};
    if (!build_data::find_ability_buckets(detail.socketEntryListIndex, selection, buckets)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    character.movementAbilityEntry = movementEntry;
    character.grenadeAbilityEntry = grenadeEntry;
    character.superAbilityEntry = superEntry;
    character.meleeAbilityEntry = meleeEntry;
    character.classAbilityEntry = classEntry;
    family4_loadout::ResolvedLoadout resolved{};
    if (!account::valid(candidate)
        || !family4_loadout::resolve(candidate, characterIndex, resolved)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    runtime::storage::g_state.account = candidate;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return true;
}

bool override_socket_plug(std::uint64_t instanceSoid,
                          std::uint8_t socketLane,
                          std::uint32_t plugDefinitionHash) noexcept {
    namespace family4_loadout = middleware::datagen::family4::loadout;
    namespace detail = runtime::detail;
    build_data::items::Definition plugDefinition{};
    if (instanceSoid == 0 || plugDefinitionHash == account::inventory::kNoDefinitionHash
        || !build_data::find_item_definition_hash(plugDefinitionHash, plugDefinition)
        || plugDefinition.definitionHash != plugDefinitionHash) {
        return false;
    }

    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    AccountState candidate = runtime::storage::g_state.account;
    std::size_t characterIndex = candidate.characterCount;
    for (std::size_t index = 0; index < candidate.characterCount; ++index) {
        if (candidate.characters[index].selected) {
            characterIndex = index;
            break;
        }
    }
    detail::CharacterItemLocation location{};
    account::inventory::Item* item = nullptr;
    build_data::items::Definition itemDefinition{};
    build_data::items::details::Definition itemDetail{};
    if (characterIndex >= candidate.characterCount
        || !detail::find_character_item_location(
            candidate.characters[characterIndex], instanceSoid, location)
        || (item = detail::character_item_at(candidate.characters[characterIndex], location))
               == nullptr
        || !build_data::find_item_definition_hash(item->definitionHash, itemDefinition)
        || !build_data::find_configured_item_detail(itemDefinition.definitionIndex, itemDetail)
        || itemDetail.definitionHash != item->definitionHash
        || socketLane >= itemDetail.ordinarySocketCount
        || item->sockets.policy != account::inventory::SocketPolicy::authored
        || item->sockets.plugCount != itemDetail.ordinarySocketCount) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }

    item->sockets.plugs[socketLane] = plugDefinitionHash;
    family4_loadout::ResolvedLoadout resolved{};
    detail::ResolvedPosition position{};
    if (!account::valid(candidate) || !family4_loadout::resolve(candidate, characterIndex, resolved)
        || !detail::find_resolved_position(resolved, instanceSoid, position)
        || position.equipped != location.equipped) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    runtime::storage::g_state.account = candidate;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return true;
}

} // namespace dawn::state
