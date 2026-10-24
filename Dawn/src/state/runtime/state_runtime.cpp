#include <Windows.h>

#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>

#include "../../core/logging/log.h"
#include "../../core/filesystem/path.h"
#include "../../core/settings/settings.h"
#include "../activity/defaults/activity_defaults_validation.h"
#include "../activity/nightfall/completion_reward.h"
#include "../activity/progress/mission_progress.h"
#include "../investment/investment_clock.h"
#include "../unlocks/unlocks_runtime.h"
#include "../build_data/runtime.h"
#include "equipment/configured_equipment_identity.h"
#include "../persistence/persistence.h"
#include "runtime.h"
#include "state.h"
#include "storage/internal.h"

namespace dawn::state {
namespace runtime::storage {

State g_state;
SRWLOCK g_stateLock{SRWLOCK_INIT};

} // namespace runtime::storage

namespace {

// Guarded by the root State lock. This is the global investment/Director clock,
// separate from an activity's scenario clock and retired only with root State.
investment_clock::Clock investmentClock;
bool newlightPending{};

/** Network-order IPv4 loopback returned by the in-process SignOn route. */
constexpr std::uint32_t kLoopbackAddress = 0x7F000001;
/** Default one-hour lifetime for generated SignOn session tokens. */
constexpr std::uint32_t kDefaultTokenLifetimeSeconds = 3600;
/** Family 5 uses the largest signed 64-bit value as its process-global object key. */
constexpr std::uint64_t kGlobalFamily5Soid =
    static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
constexpr std::wstring_view kLoadoutSaveSuffix = L"\\loadout.save";
constexpr std::array<char, 8> kLoadoutSaveMagic{'S', 'U', 'N', 'L', 'O', 'A', 'D', '1'};
core::path::Buffer g_loadoutSavePath{};
bool g_loadoutSavePathReady{};

struct LoadoutSave {
    std::array<char, 8> magic{};
    std::uint32_t version{};
    std::uint32_t accountSize{};
    AccountState account{};
};

[[nodiscard]] bool load_saved_account(AccountState& output) noexcept {
    if (!g_loadoutSavePathReady) {
        return false;
    }
    const HANDLE file = CreateFileW(g_loadoutSavePath.chars.data(), GENERIC_READ, FILE_SHARE_READ,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LoadoutSave saved{};
    DWORD read{};
    const bool readOkay = ReadFile(file, &saved, sizeof(saved), &read, nullptr) != FALSE
                          && read == sizeof(saved);
    const bool closed = CloseHandle(file) != FALSE;
    const bool okay = readOkay && closed && saved.magic == kLoadoutSaveMagic
                      && saved.version == 1 && saved.accountSize == sizeof(AccountState)
                      && account::valid(saved.account);
    if (!okay) {
        return false;
    }
    output = saved.account;
    return true;
}
/**
 * Fills fixed secret storage with Windows system randomness.
 * @tparam Size Required secret byte count.
 * @param output Secret storage to overwrite.
 * @return True when Windows generates every byte.
 */
template <std::size_t Size>
[[nodiscard]] bool randomize(std::array<std::byte, Size>& output) noexcept {
    return BCryptGenRandom(nullptr,
                           reinterpret_cast<PUCHAR>(output.data()),
                           static_cast<ULONG>(output.size()),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG)
           >= 0;
}

/** @return True when any authored or already-seeded account identity owns one SOID. */
[[nodiscard]] bool identity_uses_soid(const AccountState& accountState,
                                      std::uint64_t soid) noexcept {
    if (soid == 0 || accountState.primarySoid == soid) {
        return true;
    }
    for (std::size_t index = 0; index < accountState.profileItemCount; ++index) {
        if (accountState.profileItems[index].instanceSoid == soid) {
            return true;
        }
    }
    for (std::size_t characterIndex = 0; characterIndex < accountState.characterCount;
         ++characterIndex) {
        const CharacterState& character = accountState.characters[characterIndex];
        if (character.soid == soid) {
            return true;
        }
        for (const std::optional<account::inventory::Item>& item : character.equipment.slots) {
            if (item.has_value() && item->instanceSoid == soid) {
                return true;
            }
        }
        for (std::size_t index = 0; index < character.inventory.count; ++index) {
            if (character.inventory.values[index].instanceSoid == soid) {
                return true;
            }
        }
    }
    return false;
}

/** Seeds canonical character row generations before installed build data is needed. */
[[nodiscard]] bool seed_inventory_runtime_fields(AccountState& accountState) noexcept {
    if (!account::valid_authored(accountState)) {
        return false;
    }
    for (std::size_t characterIndex = 0; characterIndex < accountState.characterCount;
         ++characterIndex) {
        CharacterState& character = accountState.characters[characterIndex];
        std::uint32_t next = 0;
        for (std::optional<account::inventory::Item>& item : character.equipment.slots) {
            if (item.has_value()) {
                item->mutationSerial = static_cast<std::int32_t>(next++);
            }
        }
        for (std::size_t index = 0; index < character.inventory.count; ++index) {
            character.inventory.values[index].mutationSerial = static_cast<std::int32_t>(next++);
        }
        character.nextInventorySerial = next;
    }
    return account::valid(accountState);
}

/**
 * Canonicalizes only profile rows which the installed socket UI materializes as action sources.
 * @param accountState Account canonicalized in place.
 * @return True when every profile row canonicalizes.
 */
[[nodiscard]] bool canonicalize_profile_item_identities(AccountState& accountState) noexcept {
    if (!account::valid(accountState) || !build_data::socket_plug_rules_ready()) {
        return false;
    }
    std::array<bool, account::inventory::kProfileItemCapacity> actionSources{};
    std::size_t actionSourceCount = 0;
    for (std::size_t index = 0; index < accountState.profileItemCount; ++index) {
        const account::inventory::ProfileItem& profileItem = accountState.profileItems[index];
        build_data::items::Definition item{};
        build_data::items::details::Definition detail{};
        build_data::inventory::buckets::Descriptor bucket{};
        if (!build_data::find_item_definition_hash(profileItem.definitionHash, item)
            || item.definitionHash != profileItem.definitionHash
            || !build_data::find_configured_item_detail(item.definitionIndex, detail)
            || detail.definitionIndex != item.definitionIndex
            || detail.definitionHash != item.definitionHash || detail.bucketId != item.bucketId
            || detail.instancedDefinitionState
                   != build_data::items::details::InstancedDefinitionState::stackable
            || !build_data::find_inventory_bucket_descriptor(item.bucketId, bucket)
            || bucket.arraySelector != build_data::inventory::buckets::ArraySelector::profile) {
            return false;
        }
        actionSources[index] =
            build_data::is_profile_action_source(item.definitionIndex, item.bucketId);
        if (actionSources[index]
            && ++actionSourceCount > account::inventory::kProfileActionSourceCapacity) {
            return false;
        }
    }

    // Currency, material, and consumable rows are native non-instanced stacks.  Clear any stale
    // runtime key before allocating action-source identities so it cannot reserve the namespace.
    for (std::size_t index = 0; index < accountState.profileItemCount; ++index) {
        if (!actionSources[index]) {
            accountState.profileItems[index].instanceSoid = 0;
        }
    }

    std::uint64_t nextProfileSoid = account::inventory::kFirstProfileItemInstanceSoid;
    for (std::size_t index = 0; index < accountState.profileItemCount; ++index) {
        account::inventory::ProfileItem& item = accountState.profileItems[index];
        if (!actionSources[index] || item.instanceSoid != 0) {
            continue;
        }
        while (identity_uses_soid(accountState, nextProfileSoid)) {
            if (nextProfileSoid == (std::numeric_limits<std::uint64_t>::max)()) {
                return false;
            }
            ++nextProfileSoid;
        }
        item.instanceSoid = nextProfileSoid;
        if (nextProfileSoid != (std::numeric_limits<std::uint64_t>::max)()) {
            ++nextProfileSoid;
        }
    }
    return account::valid(accountState);
}

[[nodiscard]] bool same_profile_identities(const AccountState& left,
                                           const AccountState& right) noexcept {
    if (left.profileItemCount != right.profileItemCount) return false;
    for (std::size_t index = 0; index < left.profileItemCount; ++index) {
        if (left.profileItems[index].instanceSoid != right.profileItems[index].instanceSoid) {
            return false;
        }
    }
    return true;
}

} // namespace

/**
 * Loads build data and generates secrets with Dawn's authored activity defaults.
 * @param module Loaded Dawn module, or null to disable disk persistence.
 * @param initialAccount Empty State, or a complete checked account from Core settings.
 * @return True when the cached data passes its checks and every secret gets random bytes.
 */
bool initialize(void* module, const AccountState& initialAccount) noexcept {
    return initialize(module, initialAccount, activity::defaults::authored());
}

/**
 * Loads build data and publishes fixed activity defaults in one step.
 * @param module Loaded Dawn module, or null to disable disk persistence.
 * @param initialAccount Empty State, or a complete checked account from Core settings.
 * @param activityDefaults Complete local fallback policy from immutable Core settings.
 * @return True when account, defaults, cached data, and generated secrets are valid.
 */
bool initialize(void* module,
                const AccountState& initialAccount,
                const activity::defaults::ActivityDefaults& activityDefaults) noexcept {
    g_loadoutSavePath = {};
    g_loadoutSavePathReady = module != nullptr
                             && core::path::artifact_directory(module, g_loadoutSavePath)
                             && core::path::append(g_loadoutSavePath, kLoadoutSaveSuffix);
    AccountState runtimeAccount = initialAccount;
    if (load_saved_account(runtimeAccount)) {
        for (std::size_t index = 0; index < runtimeAccount.characterCount; ++index) {
            runtimeAccount.characters[index].selected = false;
        }
    }
    if (!seed_inventory_runtime_fields(runtimeAccount)
        || !activity::defaults::valid(activityDefaults)) {
        return false;
    }
    AccountState persistedAccount{};
    unlocks::ScopedTable persistedUnlocks{};
    Family5State persistedFamily5{};
    if (!persistence::initialize(module,
                                 runtimeAccount,
                                 core::settings::get().initialUnlocks,
                                 core::settings::get().initialFamily5,
                                 persistedAccount,
                                 persistedUnlocks,
                                 persistedFamily5)) {
        return false;
    }
    runtimeAccount = persistedAccount;
    if (!build_data::initialize(module, runtime::equipment::configured_hash(runtimeAccount))) {
        persistence::shutdown();
        return false;
    }
    // A cache hit already has the complete plug relation, so publish canonical profile identities
    // in the first State image.  On a first cache build, snapshot preparation repeats this step
    // after package extraction has published the relation.
    if (build_data::socket_plug_rules_ready()) {
        const AccountState beforeCanonicalization = runtimeAccount;
        if (!canonicalize_profile_item_identities(runtimeAccount)
            || (!same_profile_identities(beforeCanonicalization, runtimeAccount)
                && !persistence::commit_account(beforeCanonicalization, runtimeAccount))) {
            persistence::shutdown();
            build_data::shutdown();
            return false;
        }
    }
    {
        // The account key is authored, and a truncated one is consistent enough to go unnoticed.
        std::array<char, 96> line{};
        const int written =
            std::snprintf(line.data(),
                          line.size(),
                          "ev=account stage=identity primary=0x%016llX characters=%zu",
                          static_cast<unsigned long long>(runtimeAccount.primarySoid),
                          runtimeAccount.characterCount);
        if (written > 0) {
            core::log::write(core::log::Channel::state,
                             core::log::Level::info,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
    State initialized{};
    if (!randomize(initialized.signOn.encryptionKey)
        || !randomize(initialized.signOn.authenticationKey)
        || !randomize(initialized.signOn.sessionToken) || !randomize(initialized.bap.nonce)
        || !randomize(initialized.bap.sessionKey) || !randomize(initialized.bap.envelopeIv)) {
        SecureZeroMemory(&initialized.signOn, sizeof initialized.signOn);
        SecureZeroMemory(&initialized.bap, sizeof initialized.bap);
        initialized = {};
        persistence::shutdown();
        build_data::shutdown();
        return false;
    }
    initialized.signOn.relayAddress = kLoopbackAddress;
    // The published relay port is the one the listener binds, so both move with one setting.
    initialized.signOn.relayPort = core::settings::get().server.bapPort;
    initialized.signOn.tokenLifetimeSeconds = kDefaultTokenLifetimeSeconds;
    initialized.activity.defaults = activityDefaults;
    initialized.investment.family5.objectSoid = kGlobalFamily5Soid;
    // Only the override lists come from settings. Identity and gate stay owned by State.
    initialized.investment.family5.flags = persistedFamily5.flags;
    initialized.investment.family5.flagCount = persistedFamily5.flagCount;
    initialized.investment.family5.values = persistedFamily5.values;
    initialized.investment.family5.valueCount = persistedFamily5.valueCount;
    // This account arm also enables native character override consumption
    // (540642). Keep class/quest/vendor state enabled even when the separate
    // per-character QA bypass is off. Only that character byte skips checks.
    initialized.investment.family5.contentGateArm = true;

    investment_clock::Clock clock;
    const auto utcSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (!clock.begin(utcSeconds, GetTickCount64())) {
        SecureZeroMemory(&initialized.signOn, sizeof initialized.signOn);
        SecureZeroMemory(&initialized.bap, sizeof initialized.bap);
        initialized = {};
        persistence::shutdown();
        build_data::shutdown();
        return false;
    }
    const bool newlightReady=build_data::item_definitions_ready() && build_data::configured_item_details_ready()
        && build_data::inventory_bucket_descriptors_ready() && build_data::socket_entry_lists_ready();
    if(newlightReady) {
        const AccountState beforeNewlight=runtimeAccount;
        if(!prepare_newlight_start(runtimeAccount)
            || (beforeNewlight!=runtimeAccount && !persistence::commit_account(beforeNewlight,runtimeAccount))) {
            persistence::shutdown();build_data::shutdown();return false;
        }
    }
    initialized.account=runtimeAccount;
    // Publish one complete State and its clock only after every generated secret is valid.
    activity::nightfall::rewards::clear();
    activity::progress::reset();
    unlocks::publish(persistedUnlocks);
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    runtime::storage::g_state = initialized;
    newlightPending=!newlightReady;
    investmentClock = clock;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    SecureZeroMemory(&initialized.signOn, sizeof initialized.signOn);
    SecureZeroMemory(&initialized.bap, sizeof initialized.bap);
    initialized = {};
    return true;
}

/** Securely erases State, including activity destinations and matchmaking descriptors. */
void shutdown() noexcept {
    activity::nightfall::rewards::clear();
    activity::progress::reset();
    persistence::shutdown();
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    SecureZeroMemory(&runtime::storage::g_state.signOn, sizeof runtime::storage::g_state.signOn);
    SecureZeroMemory(&runtime::storage::g_state.bap, sizeof runtime::storage::g_state.bap);
    runtime::storage::g_state = {};
    investmentClock = {};
    newlightPending=false;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    build_data::shutdown();
    g_loadoutSavePath = {};
    g_loadoutSavePathReady = false;
}

bool save_account() noexcept {
    if (!g_loadoutSavePathReady) {
        return false;
    }
    LoadoutSave saved{kLoadoutSaveMagic, 1, sizeof(AccountState), account_snapshot()};
    if (!account::valid(saved.account)) {
        return false;
    }
    core::path::Buffer stage = g_loadoutSavePath;
    if (!core::path::append(stage, L".new")) {
        return false;
    }
    const HANDLE file = CreateFileW(stage.chars.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written{};
    bool okay = WriteFile(file, &saved, sizeof(saved), &written, nullptr) != FALSE
                && written == sizeof(saved);
    okay = CloseHandle(file) != FALSE && okay;
    okay = okay && MoveFileExW(stage.chars.data(), g_loadoutSavePath.chars.data(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!okay) {
        (void)DeleteFileW(stage.chars.data());
    }
    return okay;
}

bool load_account() noexcept {
    AccountState loaded{};
    if (!load_saved_account(loaded)) {
        return false;
    }
    const AccountState current = account_snapshot();
    std::uint64_t selectedSoid{};
    for (std::size_t index = 0; index < current.characterCount; ++index) {
        if (current.characters[index].selected) {
            selectedSoid = current.characters[index].soid;
            break;
        }
    }
    bool selectedFound = selectedSoid == 0;
    for (std::size_t index = 0; index < loaded.characterCount; ++index) {
        loaded.characters[index].selected = loaded.characters[index].soid == selectedSoid;
        selectedFound = selectedFound || loaded.characters[index].selected;
    }
    if (!selectedFound || !account::valid(loaded)) {
        return false;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    runtime::storage::g_state.account = loaded;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return true;
}

/** @return Immutable generated SignOn session fields. */
const SignOnState& sign_on() noexcept {
    return runtime::storage::g_state.signOn;
}

/** Ensures every native profile action source has one unique runtime item-instance key. */
bool ensure_profile_item_identities() noexcept {
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    AccountState candidate = runtime::storage::g_state.account;
    const bool ready = canonicalize_profile_item_identities(candidate)
        && (!newlightPending || prepare_newlight_start(candidate));
    const bool committed = ready
        && (runtime::storage::g_state.account == candidate
            || persistence::commit_account(runtime::storage::g_state.account, candidate));
    if (committed) {
        runtime::storage::g_state.account = candidate;
        newlightPending=false;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return committed;
}

/**
 * Publishes the bootstrap content-id token read from the installed client.
 * @param token Exactly 16 native bytes.
 * @return True when the complete token is kept for this process.
 */
bool publish_bootstrap_token(std::span<const std::byte> token) noexcept {
    SignOnState& signOn = runtime::storage::g_state.signOn;
    if (token.size() != signOn.bootstrapToken.size()) {
        return false;
    }
    std::copy(token.begin(), token.end(), signOn.bootstrapToken.begin());
    signOn.bootstrapTokenPresent = true;
    return true;
}

/** @return Immutable generated BAP session fields. */
const BapState& bap() noexcept {
    return runtime::storage::g_state.bap;
}

/** @return A copy of the evaluated content state, read under the lock. */
InvestmentState investment_snapshot() noexcept {
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    InvestmentState snapshot = runtime::storage::g_state.investment;
    snapshot.family5.hasTime =
        investmentClock.sample(GetTickCount64(), snapshot.family5.timeSeconds);
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return snapshot;
}

} // namespace dawn::state
