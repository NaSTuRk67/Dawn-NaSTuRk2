/**
 * Finds the three teleport targets and attaches the two detours. All or nothing: finding only some
 * would arm a key that silently does nothing.
 */

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../hooking/call_gate.h"
#include "../../hooking/detour.h"
#include "../../player/player_position.h"
#include "../bootflow/bootflow_hook_lifecycle.h"
#include "../graphics/hijacked_frame_timing.h"
#include "../fly/fly.h"
#include "../camera/runtime.h"
#include "../polled_input/runtime.h"
#include "../sword_skate/sword_skate.h"
#include "internal.h"
#include "runtime.h"

namespace dawn::client::hooks::teleport {
namespace {

/** Runs per frame on the thread owning the camera and the player, and writes the camera pose. */
constexpr std::string_view kCameraTransformText =
    "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 48 8D 6C 24 E0 48 81 EC 20 01 00 00 "
    "48 8B 05 ? ? ? ? 48 33 C4 48 89 45 10 0F 57 C0 48 63 F9";
/** Compiled pattern bytes of the camera transform signature. */
constexpr auto kCameraTransform =
    signature<signature_length(kCameraTransformText)>(kCameraTransformText);

/** Writes the local player's biped object handle, or the invalid handle value. */
constexpr std::string_view kControlledHandleText =
    "40 53 48 83 EC 20 48 8B D9 C7 01 FF FF FF FF 48 8D 4C 24 30 E8 ? ? ? ? 8B 44 24 30 "
    "83 F8 FF 74 18 25 FF 1F 00 00 0F AF 05";
/** Compiled pattern bytes of the controlled-handle signature. */
constexpr auto kControlledHandlePattern =
    signature<signature_length(kControlledHandleText)>(kControlledHandleText);

/** Runs per tick, writing the object placement from the rigid body. We must run before it. */
constexpr std::string_view kPhysicsSyncText =
    "4C 8B DC 55 53 56 41 54 41 55 49 8D 6B A1 48 81 EC F0 00 00 00 48 8B 05 ? ? ? ? "
    "48 33 C4 48 89 45 C7 44 0F B6 A9 40 02 00 00";
/** Compiled pattern bytes of the physics sync signature. */
constexpr auto kPhysicsSync = signature<signature_length(kPhysicsSyncText)>(kPhysicsSyncText);

/** The call to the camera singleton getter, measured from the camera transform's own base. */
constexpr std::size_t kSingletonCallOffset = 0x72;
/** A near call is one opcode byte and a signed displacement. */
constexpr std::byte kNearCallOpcode{0xE8};
constexpr std::size_t kNearCallOperand = 1;
constexpr std::size_t kNearCallLength = 5;

/** Both detours are installed together, so one slot each. */
constexpr std::size_t kHandleCount = 2;
constexpr std::size_t kCameraSlot = 0;
constexpr std::size_t kPhysicsSlot = 1;

using CameraTransform = std::int64_t(__fastcall*)(std::uint32_t);
using PhysicsSync = std::int64_t(__fastcall*)(std::byte*, std::byte*);

std::array<hooking::detour::Handle, kHandleCount> g_handles{};
std::atomic_bool g_installed{false};
std::atomic<CameraTransform> g_cameraOriginal{};
std::atomic<PhysicsSync> g_physicsOriginal{};
hooking::CallGate g_callGate{};

/** @return True when no camera, physics, or direct-sync call owns lifecycle state. */
[[nodiscard]] bool calls_idle() noexcept {
    return g_callGate.idle();
}

/**
 * Publishes the forward vector and reads the bound key, then defers to the original.
 * @param playerIndex Player whose camera was transformed.
 * @return Whatever the original returns.
 */
__declspec(noinline) std::int64_t __fastcall camera_transform(std::uint32_t playerIndex) noexcept {
    hooking::CallGate::Scope call(g_callGate);
    const CameraTransform next = hooking::await_original(g_cameraOriginal);
    const std::int64_t result = next(playerIndex);
    if (!call.accepts_side_effects()) {
        return result;
    }
    const graphics::hijacked_frame_timing::PostSpan timing(graphics::hijacked_frame_timing::Kind::camera_update);
    capture_forward(playerIndex);
    poll_request();
    force_pending();
    // Read here, not on the physics tick: that tick stops for a player who is standing still.
    hooks::fly::poll_toggle();
    hooks::camera::poll_toggle(playerIndex);
    client::player::position::poll();
    hooks::bootflow::poll_world_step();
    return result;
}

/**
 * Applies a pending move before the sync runs, so the sync publishes the moved position in the
 * same tick instead of overwriting it.
 * @param component Physics component being synced.
 * @param outFlags The original's second argument, untouched.
 * @return Whatever the original returns.
 */
__declspec(noinline) std::int64_t __fastcall physics_sync(std::byte* component,
                                                          std::byte* outFlags) noexcept {
    hooking::CallGate::Scope call(g_callGate);
    const PhysicsSync next = hooking::await_original(g_physicsOriginal);
    if (!call.accepts_side_effects()) {
        return next(component, outFlags);
    }
    apply_pending(component);
    // Shares this detour rather than adding a second one to the same function. The flag it clears
    // is written and read inside this tick, so it has to run here and not on a frame poll.
    hooks::sword_skate::apply(component);
    hooks::fly::apply(component);
    // This tick is the only one that sees every component, so it is where the player's is found.
    client::player::position::observe(component);
    return next(component, outFlags);
}

/**
 * Scratch space for the sync's output flags. The sync sets one bit at byte 97, so the buffer only
 * has to be big enough for that, and is never read back.
 */
constexpr std::size_t kSyncFlagsCapacity = 256;

/**
 * Decodes the camera singleton getter from the call inside the camera transform.
 * @param transform Base of the camera transform.
 * @return The getter, or null when the expected call is not there.
 */
[[nodiscard]] CameraSingleton singleton_from(std::byte* transform) noexcept {
    std::byte* const site = transform + kSingletonCallOffset;
    if (*site != kNearCallOpcode) {
        return nullptr;
    }
    return reinterpret_cast<CameraSingleton>(
        resolve_relative(site + kNearCallOperand, site + kNearCallLength));
}

/** @param reason Key naming the step that failed. @return False, for a direct return. */
[[nodiscard]] bool fail(const char* reason) noexcept {
    std::array<char, 96> line{};
    const int written = std::snprintf(
        line.data(), line.size(), "ev=teleport stage=install result=fail reason=%s", reason);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    return false;
}

} // namespace

/** Attaches the camera and physics hooks that carry the teleport. */
bool install() noexcept {
    if (g_installed.load(std::memory_order_acquire)) {
        return g_callGate.accepting();
    }
    g_callGate.quiesce();
    std::byte* const transform = scan_main_image_unique(kCameraTransform, "teleport_camera");
    if (transform == nullptr) {
        return fail("camera");
    }
    std::byte* const handle = scan_main_image_unique(kControlledHandlePattern, "teleport_handle");
    if (handle == nullptr) {
        return fail("handle");
    }
    std::byte* const sync = scan_main_image_unique(kPhysicsSync, "teleport_sync");
    if (sync == nullptr) {
        return fail("sync");
    }
    const CameraSingleton singleton = singleton_from(transform);
    if (singleton == nullptr) {
        return fail("singleton");
    }

    const std::array<hooking::detour::Spec, kHandleCount> specs{
        hooking::detour::Spec{transform, reinterpret_cast<void*>(&camera_transform)},
        hooking::detour::Spec{sync, reinterpret_cast<void*>(&physics_sync)},
    };
    if (!hooking::detour::install(specs, g_handles)) {
        return fail("attach");
    }
    hooking::publish_original(
        g_cameraOriginal, reinterpret_cast<CameraTransform>(g_handles[kCameraSlot].original));
    hooking::publish_original(
        g_physicsOriginal, reinterpret_cast<PhysicsSync>(g_handles[kPhysicsSlot].original));
    publish_targets(reinterpret_cast<ControlledHandle>(handle), singleton);
    // The injected press needs the game's key tables. Without them the move still lands, it just
    // stays invisible until the player moves, so we log instead of failing.
    if (!resolve_action_keys()) {
        (void)fail("action_keys");
    }
    g_installed.store(true, std::memory_order_release);
    g_callGate.accept();
    core::log::write(
        core::log::Channel::client, core::log::Level::info, "ev=teleport stage=install result=ok");
    return true;
}

/** Calls the physics sync for one component through the installed trampoline. */
__declspec(noinline) void invoke_sync(void* component) noexcept {
    hooking::CallGate::Scope call(g_callGate);
    if (!call.accepts_side_effects() || component == nullptr) {
        return;
    }
    const PhysicsSync next = hooking::await_original(g_physicsOriginal);
    std::array<std::byte, kSyncFlagsCapacity> flags{};
    (void)next(static_cast<std::byte*>(component), flags.data());
}

/** Stops Dawn-owned work before the camera producer is detached. */
void quiesce() noexcept {
    g_callGate.quiesce();
}

/** Detaches both teleport hooks. */
bool uninstall() noexcept {
    quiesce();
    if (!g_installed.load(std::memory_order_acquire)) {
        return true;
    }

    const std::array protectedEntries{
        hooking::detour::ProtectedCodeEntry{reinterpret_cast<void*>(&camera_transform)},
        hooking::detour::ProtectedCodeEntry{reinterpret_cast<void*>(&physics_sync)},
        hooking::detour::ProtectedCodeEntry{reinterpret_cast<void*>(&invoke_sync)},
        hooking::detour::ProtectedCodeEntry{
            reinterpret_cast<void*>(&hooking::call_gate_detail::enter)},
        hooking::detour::ProtectedCodeEntry{
            reinterpret_cast<void*>(&hooking::call_gate_detail::leave)},
    };
    const hooking::detour::UninstallResult result = hooking::detour::uninstall(
        g_handles, protectedEntries, &calls_idle);
    if (result != hooking::detour::UninstallResult::removed) {
        core::log::write(
            core::log::Channel::client,
            result == hooking::detour::UninstallResult::failed ? core::log::Level::error
                                                               : core::log::Level::warn,
            result == hooking::detour::UninstallResult::failed
                ? "ev=teleport stage=uninstall result=failed retained=1"
                : "ev=teleport stage=uninstall result=deferred retained=1");
        return false;
    }

    clear_targets();
    clear_action_keys();
    hooks::fly::reset();
    polled_input::release_key();
    g_physicsOriginal.store(nullptr, std::memory_order_release);
    g_cameraOriginal.store(nullptr, std::memory_order_release);
    g_installed.store(false, std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=teleport stage=uninstall result=ok retained=0");
    return true;
}

} // namespace dawn::client::hooks::teleport
