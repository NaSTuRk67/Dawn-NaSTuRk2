#include "group_host_sessions.h"
#include "group_host_retirement_fence.h"

#include <Windows.h>

#include <array>
#include <limits>

#if defined(DAWN_ACTIVITY_RETIREMENT_TESTS)
#include <atomic>
#endif

#include "../../../state/activity/destination/activity_destination_snapshot.h"
#include "../../../state/activity/runtime.h"
#include "../../bap/runtime.h"
#include "../gameplay_log.h"
#include "group_host.h"

#if defined(DAWN_ACTIVITY_RETIREMENT_TESTS)
#include "group_host_sessions_test_support.h"
#endif

namespace dawn::server::gameplay::group {

namespace {

/** One activity host session, keyed by its advertised group id and optional migrated alias. */
struct HostSession {
    std::uint64_t groupSessionId{};
    /** New group id installed when native host migration keeps this same activity instance. */
    std::uint64_t migratedGroupSessionId{};
    /** Exact derived activity lifetime advertised by this group row. */
    state::activity::ActivityInstanceKey hostSession{};
    /** Exact source activity lifetime whose destination this advertised host must mirror. */
    state::activity::ActivityInstanceKey sourceActivity{};
    std::uint64_t lastUse{};
    bool occupied{};
    /** Opaque row identity changes whenever a slot is repurposed. */
    std::uint64_t generation{};
    /** Active BAP snapshots pin the row through State commit and caller copy. */
    std::uint32_t pins{};
};

/** Regions that may hold an activity host session at once. */
constexpr std::size_t kHostSessionCapacity = 8;
/** Guards the host-session table. It is never held across a State allocation. */
SRWLOCK g_hostSessionLock{SRWLOCK_INIT};
/**
 * Activity host sessions this host advertises, one per region.
 * Exact keys stay here; only the session-id component crosses the gameplay compatibility edge.
 * See `activity_host_session` in `group_host.h`.
 */
std::array<HostSession, kHostSessionCapacity> g_hostSessions{};
/** Rises on every lookup, so the least recently named region is the one an eviction takes. */
std::uint64_t g_useStamp = 0;
/** Nonzero row identity allocator; retained across table resets. */
std::uint64_t g_rowGeneration = 0;
/** Sessions an eviction took the slot from. The service slice frees them. */
struct EvictedHostSession final {
    state::activity::ActivityInstanceKey host{};
    state::activity::ActivityInstanceKey source{};
};
std::array<EvictedHostSession, kHostSessionCapacity> g_evicted{};
std::size_t g_evictedCount = 0;
/** Prevents a retired source from acquiring a new derived row between detach and State cleanup. */
SourceRetirementFences g_retiringSources{};

#if defined(DAWN_ACTIVITY_RETIREMENT_TESTS)
std::atomic<test_support::Hook> g_testHook{};

void invoke_test_hook(test_support::Point point,
                      state::activity::ActivityInstanceKey source,
                      state::activity::ActivityInstanceKey allocated = {}) noexcept {
    const test_support::Hook hook = g_testHook.load(std::memory_order_acquire);
    if (hook != nullptr) {
        hook(point, source, allocated);
    }
}
#endif

/**
 * Converts an exact held lifetime to the scalar id required by the gameplay wire boundary.
 * Callers hold no host-session lock because the exact-current check takes the State lock.
 */
[[nodiscard]] std::uint64_t
public_session_id(state::activity::ActivityInstanceKey key) noexcept {
    return static_cast<bool>(key) && state::activity::contains(key)
               ? key.sessionId
               : state::activity::kAbsentSessionId;
}

/**
 * Names one region in the table, taking a slot when it holds none. The caller holds the lock.
 * @param groupSessionId Group session the region advertises.
 * @param regionIndex Region the caller is advertising, kept on the row.
 * @param sourceActivity Exact source lifetime to retain, or an absent key for defaults.
 * @param held Receives the exact session lifetime the region already holds, or an absent key.
 * @return True when a slot names the region afterwards.
 */
[[nodiscard]] bool
claim_locked(std::uint64_t groupSessionId,
             std::int32_t regionIndex,
             state::activity::ActivityInstanceKey sourceActivity,
             state::activity::ActivityInstanceKey& held) noexcept {
    held = {};
    if (source_retirement_fenced(g_retiringSources, sourceActivity)) {
        return false;
    }
    for (HostSession& entry : g_hostSessions) {
        if (entry.occupied
            && (entry.groupSessionId == groupSessionId
                || entry.migratedGroupSessionId == groupSessionId)) {
            if (source_retirement_fenced(g_retiringSources, entry.sourceActivity)) {
                return false;
            }
            entry.lastUse = ++g_useStamp;
            // A caller with no region keeps the one the advertisement recorded.
            if (regionIndex != kUnknownRegion) {
                entry.regionIndex = regionIndex;
            }
            // Readiness can claim the row before the descriptor body is encoded. Until the host
            // session is allocated, retain the latest real source so its destination is inherited.
            if (!static_cast<bool>(entry.hostSession)
                && static_cast<bool>(sourceActivity)) {
                entry.sourceActivity = sourceActivity;
            }
            held = entry.hostSession;
            return true;
        }
    }
    for (HostSession& entry : g_hostSessions) {
        if (!entry.occupied) {
            entry = {
                groupSessionId,
                0,
                {},
                sourceActivity,
                ++g_useStamp,
                regionIndex,
                true,
                ++g_rowGeneration,
                0};
            return true;
        }
    }
    if (g_evictedCount == g_evicted.size()) {
        return false;
    }
    // The evicted session is freed by the service slice, not here: this runs inside a staged push
    // and the release advances the state revision that push is committing against.
    std::size_t oldest = g_hostSessions.size();
    for (std::size_t index = 0; index < g_hostSessions.size(); ++index) {
        if (g_hostSessions[index].pins == 0
            && !source_retirement_fenced(g_retiringSources,
                                         g_hostSessions[index].sourceActivity)
            && (oldest == g_hostSessions.size()
                || g_hostSessions[index].lastUse < g_hostSessions[oldest].lastUse)) {
            oldest = index;
        }
    }
    if (oldest == g_hostSessions.size()) {
        return false;
    }
    if (static_cast<bool>(g_hostSessions[oldest].hostSession)) {
        g_evicted[g_evictedCount] = {
            g_hostSessions[oldest].hostSession,
            g_hostSessions[oldest].sourceActivity,
        };
        ++g_evictedCount;
    }
    g_hostSessions[oldest] = {
        groupSessionId,
        0,
        {},
        sourceActivity,
        ++g_useStamp,
        regionIndex,
        true,
        ++g_rowGeneration,
        0};
    return true;
}

/** Frees every session an eviction took a slot from. Callers hold no lock. */
void free_evicted_host_sessions() noexcept {
    std::array<EvictedHostSession, kHostSessionCapacity> freed{};
    std::size_t count = 0;
    AcquireSRWLockExclusive(&g_hostSessionLock);
    std::size_t retained = 0;
    for (std::size_t index = 0; index < g_evictedCount; ++index) {
        const EvictedHostSession pending = g_evicted[index];
        if (source_retirement_fenced(g_retiringSources, pending.source)) {
            g_evicted[retained] = pending;
            ++retained;
        } else {
            freed[count] = pending;
            ++count;
        }
    }
    for (std::size_t index = retained; index < g_evicted.size(); ++index) {
        g_evicted[index] = {};
    }
    g_evictedCount = retained;
    ReleaseSRWLockExclusive(&g_hostSessionLock);
    for (std::size_t index = 0; index < count; ++index) {
        const state::activity::ActivityInstanceKey key = freed[index].host;
        const state::activity::RetireResult result =
            bap::retire_group_owned_activity(key);
        report(core::log::Level::info,
               "ev=gameplay stage=activityhost result=evicted session=0x%llX incarnation=%llu "
               "state=%s",
               static_cast<unsigned long long>(key.sessionId),
               static_cast<unsigned long long>(key.incarnation.value),
               result == state::activity::RetireResult::retired ? "freed" : "absent");
    }
}

} // namespace

/** Acquires a stable exact row lineage lease. */
bool acquire_host_activity_lineage(state::activity::ActivityInstanceKey host,
                                   HostActivityLineageLease& output) noexcept {
    output = {};
    if (!static_cast<bool>(host)) {
        return false;
    }
    bool acquired = false;
    AcquireSRWLockExclusive(&g_hostSessionLock);
    for (std::size_t index = 0; index < g_hostSessions.size(); ++index) {
        HostSession& entry = g_hostSessions[index];
        if (!entry.occupied || entry.hostSession != host
            || !static_cast<bool>(entry.sourceActivity)
            || source_retirement_fenced(g_retiringSources, entry.sourceActivity)
            || entry.pins == (std::numeric_limits<std::uint32_t>::max)()) {
            continue;
        }
        ++entry.pins;
        output.groupSessionId = entry.groupSessionId;
        output.host = entry.hostSession;
        output.source = entry.sourceActivity;
        output.regionIndex = entry.regionIndex;
        output.rowGeneration = entry.generation;
        output.rowSlot = static_cast<std::uint8_t>(index);
        output.pinned = true;
        acquired = true;
        break;
    }
    ReleaseSRWLockExclusive(&g_hostSessionLock);
    return acquired;
}

/** Revalidates one held row pin. */
bool validate_host_activity_lineage(const HostActivityLineageLease& lease) noexcept {
    if (!lease.pinned || lease.rowSlot >= g_hostSessions.size()) {
        return false;
    }
    bool valid = false;
    AcquireSRWLockShared(&g_hostSessionLock);
    const HostSession& entry = g_hostSessions[lease.rowSlot];
    valid = entry.occupied && entry.pins != 0 && entry.generation == lease.rowGeneration
            && entry.groupSessionId == lease.groupSessionId && entry.hostSession == lease.host
            && entry.sourceActivity == lease.source && entry.regionIndex == lease.regionIndex;
    ReleaseSRWLockShared(&g_hostSessionLock);
    return valid;
}

/** Releases one row pin. */
void release_host_activity_lineage(HostActivityLineageLease& lease) noexcept {
    if (!lease.pinned || lease.rowSlot >= g_hostSessions.size()) {
        lease = {};
        return;
    }
    AcquireSRWLockExclusive(&g_hostSessionLock);
    HostSession& entry = g_hostSessions[lease.rowSlot];
    if (entry.occupied && entry.generation == lease.rowGeneration
        && entry.hostSession == lease.host && entry.sourceActivity == lease.source
        && entry.pins != 0) {
        --entry.pins;
    }
    ReleaseSRWLockExclusive(&g_hostSessionLock);
    lease = {};
    bap::retry_pending_activity_retirements();
}

/** Reports the activity host session already held for one region, without claiming a slot. */
std::uint64_t held_host_session(std::uint64_t groupSessionId) noexcept {
    return held_host_activity(groupSessionId).sessionId;
}

/** Reports the exact current activity host lifetime already held for one region. */
state::activity::ActivityInstanceKey
held_host_activity(std::uint64_t groupSessionId) noexcept {
    state::activity::ActivityInstanceKey held{};
    AcquireSRWLockShared(&g_hostSessionLock);
    for (const HostSession& entry : g_hostSessions) {
        if (entry.occupied
            && (entry.groupSessionId == groupSessionId
                || entry.migratedGroupSessionId == groupSessionId)) {
            held = entry.hostSession;
            break;
        }
    }
    ReleaseSRWLockShared(&g_hostSessionLock);
    if (!static_cast<bool>(held) || state::activity::contains(held)) {
        return held;
    }
    // State validation happens outside the group lock. Clear only the exact stale value observed;
    // a concurrently installed successor must survive this cleanup.
    AcquireSRWLockExclusive(&g_hostSessionLock);
    for (HostSession& entry : g_hostSessions) {
        if (entry.occupied && entry.hostSession == held
            && (entry.groupSessionId == groupSessionId
                || entry.migratedGroupSessionId == groupSessionId)) {
            entry.hostSession = {};
        }
    }
    ReleaseSRWLockExclusive(&g_hostSessionLock);
    return {};
}

/** Makes a native host-migration group id share its source group's activity host. */
std::uint64_t alias_host_session(std::uint64_t sourceGroupSessionId,
                                 std::uint64_t migratedGroupSessionId) noexcept {
    if (sourceGroupSessionId == 0 || migratedGroupSessionId == 0) {
        return state::activity::kAbsentSessionId;
    }
    state::activity::ActivityInstanceKey held{};
    AcquireSRWLockExclusive(&g_hostSessionLock);
    HostSession* source = nullptr;
    HostSession* conflicting = nullptr;
    for (HostSession& entry : g_hostSessions) {
        if (!entry.occupied) {
            continue;
        }
        if (entry.groupSessionId == sourceGroupSessionId
            || entry.migratedGroupSessionId == sourceGroupSessionId) {
            source = &entry;
        }
        if (entry.groupSessionId == migratedGroupSessionId
            || entry.migratedGroupSessionId == migratedGroupSessionId) {
            conflicting = &entry;
        }
    }
    if (source != nullptr && (conflicting == nullptr || conflicting == source)) {
        source->migratedGroupSessionId = migratedGroupSessionId;
        source->lastUse = ++g_useStamp;
        held = source->hostSession;
    }
    ReleaseSRWLockExclusive(&g_hostSessionLock);
    return public_session_id(held);
}

/** Copies every occupied host-session row. */
void snapshot_host_sessions(std::span<HostSessionRow> output, std::size_t& count) noexcept {
    count = 0;
    std::array<HostSession, kHostSessionCapacity> selected{};
    std::size_t selectedCount = 0;
    AcquireSRWLockShared(&g_hostSessionLock);
    for (HostSession& entry : g_hostSessions) {
        if (!entry.occupied || selectedCount >= output.size()) {
            continue;
        }
        selected[selectedCount] = entry;
        ++selectedCount;
    }
    ReleaseSRWLockShared(&g_hostSessionLock);
    for (std::size_t index = 0; index < selectedCount; ++index) {
        const HostSession& entry = selected[index];
        output[count] = {
            entry.groupSessionId, public_session_id(entry.hostSession), entry.regionIndex};
        ++count;
    }
}

/** Reports one region's activity session, asking the gameplay slice to allocate a missing one. */
std::uint64_t activity_host_session(std::uint64_t groupSessionId) noexcept {
    bool claimed = false;
    return activity_host_session(
        groupSessionId, regionIndex, state::activity::kAbsentSessionId, claimed);
}

/** Reports one region's activity session and whether it holds a slot at all. */
std::uint64_t activity_host_session(std::uint64_t groupSessionId,
                                    std::int32_t regionIndex,
                                    std::uint64_t sourceActivitySessionId,
                                    bool& claimedSlot) noexcept {
    claimedSlot = false;
    if (groupSessionId == 0) {
        return state::activity::kAbsentSessionId;
    }
    state::activity::ActivityInstanceKey sourceActivity{};
    if (sourceActivitySessionId != state::activity::kAbsentSessionId
        && !state::activity::snapshot_instance_key(sourceActivitySessionId, sourceActivity)) {
        // A nonzero source means inheritance was explicitly requested. If that lifetime has
        // retired, allocating from defaults would silently create a different activity.
        report(core::log::Level::warn,
               "ev=gameplay stage=activityhost result=stale_source source=0x%llX",
               static_cast<unsigned long long>(sourceActivitySessionId));
        return state::activity::kAbsentSessionId;
    }
    return activity_host_session(groupSessionId, regionIndex, sourceActivity, claimedSlot);
}

/** Reports one region's activity session for one exact source activity lifetime. */
std::uint64_t
activity_host_session(std::uint64_t groupSessionId,
                      std::int32_t regionIndex,
                      state::activity::ActivityInstanceKey sourceActivity,
                      bool& claimedSlot) noexcept {
    claimedSlot = false;
    if (groupSessionId == 0) {
        return state::activity::kAbsentSessionId;
    }
    if (static_cast<bool>(sourceActivity) && !state::activity::contains(sourceActivity)) {
        report(core::log::Level::warn,
               "ev=gameplay stage=activityhost result=stale_source source=0x%llX incarnation=%llu",
               static_cast<unsigned long long>(sourceActivity.sessionId),
               static_cast<unsigned long long>(sourceActivity.incarnation.value));
        return state::activity::kAbsentSessionId;
    }
#if defined(DAWN_ACTIVITY_RETIREMENT_TESTS)
    if (static_cast<bool>(sourceActivity)) {
        invoke_test_hook(test_support::Point::sourcePrecheckComplete, sourceActivity);
    }
#endif
    // Never allocated here. The allocation advances the state revision and would fail the guard
    // of the push that called in. The slot is claimed now and `service` fills it for the next one.
    state::activity::ActivityInstanceKey held{};
    AcquireSRWLockExclusive(&g_hostSessionLock);
    const bool claimed = claim_locked(groupSessionId, regionIndex, sourceActivity, held);
    ReleaseSRWLockExclusive(&g_hostSessionLock);
    claimedSlot = claimed;
    if (!claimed) {
        // No slot, so nothing will ever fill one. A caller waiting on this has to publish without
        // it rather than hold for an allocation that is not coming.
        report(core::log::Level::warn, "ev=gameplay stage=activityhost result=full");
    }
    if (claimed && static_cast<bool>(sourceActivity)
        && !state::activity::contains(sourceActivity)) {
        // A caller may have validated State before waiting behind the source fence. Revalidate
        // after the claim lock too; once the fence is removed, the exact source is already gone.
        AcquireSRWLockExclusive(&g_hostSessionLock);
        for (HostSession& entry : g_hostSessions) {
            if (entry.occupied && entry.pins == 0 && !static_cast<bool>(entry.hostSession)
                && entry.sourceActivity == sourceActivity
                && (entry.groupSessionId == groupSessionId
                    || entry.migratedGroupSessionId == groupSessionId)) {
                entry = {};
            }
        }
        ReleaseSRWLockExclusive(&g_hostSessionLock);
        claimedSlot = false;
        return state::activity::kAbsentSessionId;
    }
    const std::uint64_t published = public_session_id(held);
    if (static_cast<bool>(held) && published == state::activity::kAbsentSessionId) {
        // The row outlived its exact State record. Clear only that observed lifetime so the next
        // service slice can allocate a successor instead of reporting pending forever.
        AcquireSRWLockExclusive(&g_hostSessionLock);
        for (HostSession& entry : g_hostSessions) {
            if (entry.occupied && entry.hostSession == held
                && (entry.groupSessionId == groupSessionId
                    || entry.migratedGroupSessionId == groupSessionId)) {
                entry.hostSession = {};
                if (static_cast<bool>(sourceActivity)) {
                    entry.sourceActivity = sourceActivity;
                }
            }
        }
        ReleaseSRWLockExclusive(&g_hostSessionLock);
    }
    return published;
}

/** Fills every claimed host-session slot that has no session yet. */
void allocate_claimed_host_sessions() noexcept {
    free_evicted_host_sessions();
    for (;;) {
        std::uint64_t groupSessionId = 0;
        state::activity::ActivityInstanceKey sourceActivity{};
        AcquireSRWLockShared(&g_hostSessionLock);
        for (const HostSession& entry : g_hostSessions) {
            if (entry.occupied && !static_cast<bool>(entry.hostSession)
                && !source_retirement_fenced(g_retiringSources, entry.sourceActivity)) {
                groupSessionId = entry.groupSessionId;
                sourceActivity = entry.sourceActivity;
                break;
            }
        }
        ReleaseSRWLockShared(&g_hostSessionLock);
        if (groupSessionId == 0) {
            return;
        }
        // Outside the table lock: the allocation takes the State lock and the two may not nest.
        std::uint64_t sessionId = state::activity::kAbsentSessionId;
        state::activity::PendingAllocation allocation{};
        state::activity::destination::DestinationSelection sourceDestination{};
        const bool hasSource = static_cast<bool>(sourceActivity);
        const bool inherited =
            hasSource
            && state::activity::destination::snapshot(sourceActivity, sourceDestination);
        if (hasSource && !inherited) {
            // The source retired after its exact key was claimed. Drop only this still-unfilled
            // row; keeping it would wedge the allocator on the same stale source every slice.
            report(core::log::Level::warn,
                   "ev=gameplay stage=activityhost result=stale_source source=0x%llX "
                   "incarnation=%llu",
                   static_cast<unsigned long long>(sourceActivity.sessionId),
                   static_cast<unsigned long long>(sourceActivity.incarnation.value));
            AcquireSRWLockExclusive(&g_hostSessionLock);
            for (HostSession& entry : g_hostSessions) {
                if (entry.occupied && entry.groupSessionId == groupSessionId
                    && !static_cast<bool>(entry.hostSession)
                    && entry.sourceActivity == sourceActivity) {
                    entry = {};
                }
            }
            ReleaseSRWLockExclusive(&g_hostSessionLock);
            continue;
        }
        const bool prepared =
            inherited
                ? state::activity::prepare_session(sourceDestination, sessionId, allocation)
                : state::activity::prepare_session(sessionId, allocation);
        const state::activity::ActivityInstanceKey allocated = allocation.instanceKey;
        if (!prepared || !static_cast<bool>(allocated)
            || !state::activity::commit(allocation)) {
            // The account half is not loaded yet on an early slice, so this retries next slice.
            return;
        }
#if defined(DAWN_ACTIVITY_RETIREMENT_TESTS)
        invoke_test_hook(
            test_support::Point::allocationCommittedBeforeStore, sourceActivity, allocated);
#endif
        bool stored = false;
        std::size_t occupied = 0;
        AcquireSRWLockExclusive(&g_hostSessionLock);
        for (HostSession& entry : g_hostSessions) {
            if (!entry.occupied) {
                continue;
            }
            ++occupied;
            if (entry.groupSessionId == groupSessionId
                && !static_cast<bool>(entry.hostSession)
                && entry.sourceActivity == sourceActivity
                && !source_retirement_fenced(g_retiringSources, sourceActivity)) {
                entry.hostSession = allocated;
                stored = true;
            }
        }
        ReleaseSRWLockExclusive(&g_hostSessionLock);
        if (!stored) {
            // The slot was evicted or changed source while this was allocating, so only this
            // exact newly allocated lifetime may be retired.
            static_cast<void>(bap::retire_group_owned_activity(allocated));
            return;
        }
        report(core::log::Level::info,
               "ev=gameplay stage=activityhost result=allocated session=0x%llX group=0x%016llX "
               "incarnation=%llu source=0x%llX source_incarnation=%llu inherited=%u held=%zu",
               static_cast<unsigned long long>(allocated.sessionId),
               static_cast<unsigned long long>(groupSessionId),
               static_cast<unsigned long long>(allocated.incarnation.value),
               static_cast<unsigned long long>(sourceActivity.sessionId),
               static_cast<unsigned long long>(sourceActivity.incarnation.value),
               inherited ? 1U : 0U,
               occupied);
    }
}

/** Fences one exact source and snapshots every unpinned derived owner for recursive retirement. */
bool begin_host_session_source_retirement(
    state::activity::ActivityInstanceKey source,
    std::span<state::activity::ActivityInstanceKey> output,
    std::size_t& count) noexcept {
    count = 0;
    if (!static_cast<bool>(source)) {
        return false;
    }
    AcquireSRWLockExclusive(&g_hostSessionLock);
    if (!begin_source_retirement(g_retiringSources, source)) {
        ReleaseSRWLockExclusive(&g_hostSessionLock);
        return false;
    }
    for (const HostSession& entry : g_hostSessions) {
        if (entry.occupied && entry.sourceActivity == source && entry.pins != 0) {
            ReleaseSRWLockExclusive(&g_hostSessionLock);
            return false;
        }
    }
    for (const HostSession& entry : g_hostSessions) {
        if (!entry.occupied || entry.sourceActivity != source) {
            continue;
        }
        if (static_cast<bool>(entry.hostSession) && count < output.size()) {
            output[count] = entry.hostSession;
            ++count;
        }
    }
    for (std::size_t index = 0; index < g_evictedCount; ++index) {
        const EvictedHostSession pending = g_evicted[index];
        if (pending.source == source) {
            if (static_cast<bool>(pending.host) && count < output.size()) {
                output[count] = pending.host;
                ++count;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_hostSessionLock);
    return true;
}

/** Clears one exact derived owner after its recursive retirement completes. */
void commit_host_session_derived_retirement(
    state::activity::ActivityInstanceKey source,
    state::activity::ActivityInstanceKey derived) noexcept {
    AcquireSRWLockExclusive(&g_hostSessionLock);
    for (HostSession& entry : g_hostSessions) {
        if (entry.occupied && entry.pins == 0 && entry.sourceActivity == source
            && entry.hostSession == derived) {
            entry = {};
        }
    }
    std::size_t retained = 0;
    for (std::size_t index = 0; index < g_evictedCount; ++index) {
        if (g_evicted[index].source == source && g_evicted[index].host == derived) {
            continue;
        }
        g_evicted[retained] = g_evicted[index];
        ++retained;
    }
    for (std::size_t index = retained; index < g_evicted.size(); ++index) {
        g_evicted[index] = {};
    }
    g_evictedCount = retained;
    ReleaseSRWLockExclusive(&g_hostSessionLock);
}

/** Clears a fenced source's rows after the copied derived lifetimes are retired. */
void commit_host_session_source_retirement(
    state::activity::ActivityInstanceKey source) noexcept {
    AcquireSRWLockExclusive(&g_hostSessionLock);
    for (HostSession& entry : g_hostSessions) {
        if (entry.occupied && entry.sourceActivity == source && entry.pins == 0) {
            entry = {};
        }
    }
    std::size_t retained = 0;
    for (std::size_t index = 0; index < g_evictedCount; ++index) {
        if (g_evicted[index].source == source) {
            continue;
        }
        g_evicted[retained] = g_evicted[index];
        ++retained;
    }
    for (std::size_t index = retained; index < g_evicted.size(); ++index) {
        g_evicted[index] = {};
    }
    g_evictedCount = retained;
    ReleaseSRWLockExclusive(&g_hostSessionLock);
}

/** Removes one source tombstone after its exact State record is no longer live. */
void finish_host_session_source_retirement(
    state::activity::ActivityInstanceKey source) noexcept {
    AcquireSRWLockExclusive(&g_hostSessionLock);
    finish_source_retirement(g_retiringSources, source);
    ReleaseSRWLockExclusive(&g_hostSessionLock);
}

/** Returns every held host session to State and clears the table. */
void reset_host_sessions() noexcept {
    // Dropping the table alone would strand its records in State, and nothing else can name them
    // once their group session is forgotten.
    std::array<state::activity::ActivityInstanceKey, kHostSessionCapacity> held{};
    std::size_t count = 0;
    AcquireSRWLockExclusive(&g_hostSessionLock);
    for (HostSession& entry : g_hostSessions) {
        if (entry.occupied && entry.pins == 0
            && !source_retirement_fenced(g_retiringSources, entry.sourceActivity)) {
            held[count] = entry.hostSession;
            ++count;
            entry = {};
        }
    }
    ReleaseSRWLockExclusive(&g_hostSessionLock);
    for (std::size_t index = 0; index < count; ++index) {
        static_cast<void>(bap::retire_group_owned_activity(held[index]));
    }
    free_evicted_host_sessions();
}

#if defined(DAWN_ACTIVITY_RETIREMENT_TESTS)
namespace test_support {

void set_hook(Hook hook) noexcept {
    g_testHook.store(hook, std::memory_order_release);
}

Snapshot snapshot() noexcept {
    Snapshot copied{};
    AcquireSRWLockShared(&g_hostSessionLock);
    const auto add_unique = [&copied](state::activity::ActivityInstanceKey key) noexcept {
        if (!static_cast<bool>(key)) {
            return;
        }
        for (std::size_t index = 0; index < copied.uniqueOwnerCount; ++index) {
            if (copied.uniqueOwners[index] == key) {
                return;
            }
        }
        if (copied.uniqueOwnerCount < copied.uniqueOwners.size()) {
            copied.uniqueOwners[copied.uniqueOwnerCount] = key;
            ++copied.uniqueOwnerCount;
        }
    };
    for (const HostSession& entry : g_hostSessions) {
        if (!entry.occupied) {
            continue;
        }
        ++copied.occupiedRows;
        if (entry.pins != 0) {
            ++copied.pinnedRows;
        }
        if (static_cast<bool>(entry.hostSession)) {
            ++copied.currentOwners;
            add_unique(entry.hostSession);
        } else {
            ++copied.unfilledRows;
        }
    }
    for (std::size_t index = 0; index < g_evictedCount; ++index) {
        if (static_cast<bool>(g_evicted[index].host)) {
            ++copied.pendingOwners;
            add_unique(g_evicted[index].host);
        }
    }
    copied.fenceCount = g_retiringSources.count;
    ReleaseSRWLockShared(&g_hostSessionLock);
    return copied;
}

bool seed_current(std::uint64_t groupSessionId,
                  state::activity::ActivityInstanceKey host,
                  state::activity::ActivityInstanceKey source,
                  std::uint32_t pins) noexcept {
    if (groupSessionId == 0 || !static_cast<bool>(host)) {
        return false;
    }
    bool stored = false;
    AcquireSRWLockExclusive(&g_hostSessionLock);
    for (HostSession& entry : g_hostSessions) {
        if (entry.occupied) {
            continue;
        }
        entry = {
            groupSessionId,
            0,
            host,
            source,
            ++g_useStamp,
            0,
            true,
            ++g_rowGeneration,
            pins,
        };
        stored = true;
        break;
    }
    ReleaseSRWLockExclusive(&g_hostSessionLock);
    return stored;
}

bool seed_pending(state::activity::ActivityInstanceKey host,
                  state::activity::ActivityInstanceKey source) noexcept {
    if (!static_cast<bool>(host)) {
        return false;
    }
    bool stored = false;
    AcquireSRWLockExclusive(&g_hostSessionLock);
    if (g_evictedCount < g_evicted.size()) {
        g_evicted[g_evictedCount] = {host, source};
        ++g_evictedCount;
        stored = true;
    }
    ReleaseSRWLockExclusive(&g_hostSessionLock);
    return stored;
}

void reset_storage() noexcept {
    AcquireSRWLockExclusive(&g_hostSessionLock);
    g_hostSessions = {};
    g_useStamp = 0;
    g_rowGeneration = 0;
    g_evicted = {};
    g_evictedCount = 0;
    g_retiringSources = {};
    ReleaseSRWLockExclusive(&g_hostSessionLock);
    set_hook(nullptr);
}

} // namespace test_support
#endif

} // namespace dawn::server::gameplay::group
