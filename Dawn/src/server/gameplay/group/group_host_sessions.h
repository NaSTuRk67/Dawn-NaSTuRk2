#pragma once

#include <cstdint>

#include "../../../state/activity/lifecycle_generation.h"

namespace dawn::server::gameplay::group {

/** Region of a caller that knows none. Such a call keeps the region already on the row. */
inline constexpr std::int32_t kUnknownRegion = -1;
/** Largest derived-record batch detached by a source retirement. */
inline constexpr std::size_t kHostRecordRetirementCapacity = 16;

/** Stable copied lineage plus an opaque pin on one group-owned advertised host row. */
struct HostActivityLineageLease final {
    std::uint64_t groupSessionId{};
    state::activity::ActivityInstanceKey host{};
    state::activity::ActivityInstanceKey source{};
    std::int32_t regionIndex{kUnknownRegion};
    std::uint64_t rowGeneration{};
    std::uint8_t rowSlot{0xFF};
    bool pinned{};
};

/** Acquires a stable exact host -> creator-source row pin. */
[[nodiscard]] bool acquire_host_activity_lineage(
    state::activity::ActivityInstanceKey host,
    HostActivityLineageLease& output) noexcept;

/** Revalidates a held row pin without consulting State. */
[[nodiscard]] bool validate_host_activity_lineage(
    const HostActivityLineageLease& lease) noexcept;

/** Releases one row pin. Repeated release is harmless. */
void release_host_activity_lineage(HostActivityLineageLease& lease) noexcept;

/**
 * One region's advertised group session and its wire-compatible scalar activity host id.
 * Internally the table owns an exact activity-instance key; snapshots expose only the session-id
 * component because the existing HUD/debugging interface is scalar.
 */
struct HostSessionRow {
    std::uint64_t groupSessionId{};
    std::uint64_t hostSessionId{};
    std::int32_t regionIndex{};
};

/**
 * Reports the activity host session already held for one region, without claiming a slot.
 * @param groupSessionId Group session the region advertises.
 * The table validates its exact activity-instance key after dropping its own lock, then exposes
 * only the session-id component for compatibility with gameplay messages.
 * @return The current host session id, or the absent id when none is held yet or the exact
 *         lifetime has retired.
 */
[[nodiscard]] std::uint64_t held_host_session(std::uint64_t groupSessionId) noexcept;

/** Returns the exact current activity host lifetime without truncating it. */
[[nodiscard]] state::activity::ActivityInstanceKey
held_host_activity(std::uint64_t groupSessionId) noexcept;

/**
 * Makes a host-migration group id resolve to the activity host already owned by its source group.
 * Native host migration changes the group identifier without changing the activity instance. The
 * old id remains valid for the reliable link while the new id becomes the session-message key, so
 * both must resolve to the same host until the old link is retired.
 * @param sourceGroupSessionId Group id that already owns the host session.
 * @param migratedGroupSessionId New group id installed by native host migration.
 * @return The shared current host session id for wire compatibility, or the absent id when the
 *         source owns none or its exact activity lifetime has retired.
 */
[[nodiscard]] std::uint64_t alias_host_session(std::uint64_t sourceGroupSessionId,
                                               std::uint64_t migratedGroupSessionId) noexcept;

/** Copies every occupied host-session row. @param count Receives the copied row count. */
void snapshot_host_sessions(std::span<HostSessionRow> output, std::size_t& count) noexcept;

/**
 * Fills every claimed host-session slot that has no session yet, and frees every evicted session.
 * The allocation advances the state revision, so it must never run inside a staged push. That
 * push would fail its own revision guard. Callers hold no lock.
 */
void allocate_claimed_host_sessions() noexcept;

/**
 * Fences one exact source and snapshots its derived owners without detaching them.
 * Ownership stays visible until the caller commits each completed derived retirement. A pinned
 * row leaves the fence installed and returns false so the caller can retain a durable retry.
 * The caller holds no group lock.
 */
[[nodiscard]] bool begin_host_session_source_retirement(
    state::activity::ActivityInstanceKey source,
    std::span<state::activity::ActivityInstanceKey> output,
    std::size_t& count) noexcept;

/** Transfers one completed derived owner out of its fenced source row exactly once. */
void commit_host_session_derived_retirement(
    state::activity::ActivityInstanceKey source,
    state::activity::ActivityInstanceKey derived) noexcept;

/** Clears the fenced source's rows after every copied derived record has retired. */
void commit_host_session_source_retirement(
    state::activity::ActivityInstanceKey source) noexcept;

/** Removes an exact source fence only after its State record has retired. */
void finish_host_session_source_retirement(
    state::activity::ActivityInstanceKey source) noexcept;

/** Returns every held host session to State and clears the table. */
void reset_host_sessions() noexcept;

} // namespace dawn::server::gameplay::group
