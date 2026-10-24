#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::client::hooks::teleport {

/** Three floats make one position or velocity vector. */
inline constexpr std::size_t kVectorLanes = 3;
/** The vertical lane. The camera basis is X forward, Z up, so the third lane is up. */
inline constexpr std::size_t kVerticalLane = 2;

/** One world-space vector as the game stores it. */
using Vector = std::array<float, kVectorLanes>;

/** Writes the local player's controlled-object handle, or the invalid sentinel. */
using ControlledHandle = std::uint32_t* (*)(std::uint32_t*);
/** Returns the camera pose block array. The pointer in its global is obfuscated, so we call it. */
using CameraSingleton = std::byte* (*)();

/** Reads the native controlled entity even when its physics body is asleep. Game thread only. */
[[nodiscard]] bool read_controlled_entity(std::uint32_t& entity) noexcept;

/**
 * Publishes the two functions the hooks call.
 * @param controlled Writes the local player's object handle.
 * @param singleton Returns the camera pose block array.
 */
void publish_targets(ControlledHandle controlled, CameraSingleton singleton) noexcept;

/** Drops those functions and every latched request. */
void clear_targets() noexcept;

[[nodiscard]] bool is_controlled_object(const void* object) noexcept;

[[nodiscard]] bool current_position(std::array<float, 3>& output) noexcept;

[[nodiscard]] bool current_controlled_handle(std::uint32_t& output) noexcept;

[[nodiscard]] bool current_camera_pose(std::array<float, 3>& position,
                                       std::array<float, 3>& forward) noexcept;

/**
 * Attaches the camera and physics hooks that carry the teleport.
 * @return True when all three targets were found and both detours attached.
 */
[[nodiscard]] bool install() noexcept;

/** Stops Dawn-owned camera/physics work while both native forwarding paths remain live. */
void quiesce() noexcept;

/** Detaches both teleport hooks, retaining their state when protected calls are active. */
[[nodiscard]] bool uninstall() noexcept;

/**
 * Publishes the camera forward vector for the physics tick that follows.
 * @param playerIndex Player the camera pose block belongs to.
 */
void capture_forward(std::uint32_t playerIndex) noexcept;

/** Latches one teleport request if the bound key went down this frame. */
void poll_request() noexcept;

/** Runs the move for a request no physics tick collected, and drives the sync for it. */
void force_pending() noexcept;

/**
 * Finds both key tables from the polled keyboard scan.
 * @return True when the scan and both of its table loads were found.
 */
[[nodiscard]] bool resolve_action_keys() noexcept;

/** Drops both key tables. */
void clear_action_keys() noexcept;

/**
 * Turns one authored binding into the virtual key the scan will read.
 * @param binding Input code taken from an authored binding half.
 * @return The virtual key, or 0 when there is none.
 */
[[nodiscard]] std::uint32_t action_key(std::uint16_t binding) noexcept;

/**
 * Calls the physics sync for one component through the installed trampoline.
 * @param component Physics component to sync.
 */
void invoke_sync(void* component) noexcept;

/**
 * Moves the local player if a request is pending and this component owns them.
 * @param component Physics component about to be synced.
 */
void apply_pending(void* component) noexcept;

/**
 * @param component Candidate physics component.
 * @return True when it drives the object the local player controls.
 *
 * Exposed because the physics sync is the only tick that sees every component, and a feature that
 * has to act on the player's own tick needs the same test this module already performs.
 */
[[nodiscard]] bool owns_local_player(void* component) noexcept;

/** Read-only full salted entity identity, revalidated around the component read. */
[[nodiscard]] bool read_local_player_entity(void* component,std::uint32_t& entity) noexcept;

/**
 * Reads the world position of the body a physics component drives.
 * @param component Physics component.
 * @param position Receives the three lanes.
 * @return True when the body was found and read.
 */
[[nodiscard]] bool read_position(void* component, Vector& position) noexcept;

/**
 * Writes the world position of the body a physics component drives.
 * @param component Physics component.
 * @param position Three lanes to store.
 * @return True when the body was found and written.
 */
[[nodiscard]] bool write_position(void* component, const Vector& position) noexcept;

/**
 * Reads the linear velocity of the body a physics component drives.
 * @param component Physics component.
 * @param velocity Receives the three lanes.
 * @return True when the body was found and read.
 */
[[nodiscard]] bool read_velocity(void* component, Vector& velocity) noexcept;

/**
 * Writes the linear velocity of the body a physics component drives.
 * @param component Physics component.
 * @param velocity Three lanes to store.
 * @return True when the body was found and written.
 */
[[nodiscard]] bool write_velocity(void* component, const Vector& velocity) noexcept;

/**
 * The camera hook is the only site that sees the pose block, so it publishes the vector here.
 * @param forward Receives the camera forward vector published this frame.
 * @return True once the camera hook has published one.
 */
[[nodiscard]] bool camera_forward(Vector& forward) noexcept;

/**
 * Queues one absolute move of the local player's body to a world position. The next physics
 * tick that owns the player applies it, regardless of the panel teleport toggle. Used by the
 * Omega gate hop.
 * @param target World position to place the body at.
 */
void request_absolute_move(const Vector& target) noexcept;

/**
 * Queues one move of the local player's body to the NEAREST point of a named spawn set. The
 * next physics tick that owns the player resolves the point from its own body position and
 * applies it, so no cached component or prior teleport use is required.
 * @param setHash FNV-1 of the spawn-set name; zero clears nothing and is ignored.
 */
void request_move_to_spawn_set(std::uint32_t setHash) noexcept;

/**
 * Turns the first-person view to a world direction for the next ~0.75 s of camera frames.
 * The camera stores its aim as forward-vector copies, so the turn is applied by rewriting
 * them until the controller carries the new heading. Used by the Omega gate hop.
 * @param direction Unit world direction to face.
 */
void request_face_direction(const Vector& direction) noexcept;

} // namespace dawn::client::hooks::teleport
