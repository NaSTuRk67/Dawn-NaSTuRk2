#include "../../../state/activity/Newlight/launchpad/runtime.h"
#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../../state/activity/runtime.h"
#include "../../../state/activity/vanilla/one_au/runtime.h"
#include "../../../state/activity/vanilla/homecoming/runtime.h"
#include "../../../state/activity/vanilla/adieu/runtime.h"
#include "../../../server/bap/runtime.h"
#include "../../../state/activity/vanilla/homecoming/prologue.h"
#include "../../hooking/call_gate.h"
#include "../../hooking/detour.h"
#include "bootflow_hook_lifecycle.h"
#include "internal.h"
#include "hijacked_placements.h"
#include "forest_candy_drops.h"
#include "native_round_player_probe.h"
#include "../graphics/hijacked_frame_timing.h"
#include "omega_enemy_lair_receipts.h"
#include "../../activity/mission_launch.h"
#include "../../../state/activity/gateway_intro.h"
#include "omega_activity_handoff.inl"
#include "launchpad_handoff.inl"

namespace dawn::client::hooks::bootflow {
namespace {

/**
 * `LoadingCinematics_Suppressed` wrapper @ rva 0xC24490: `sub rsp,28; call <stub>; test al,al;
 * setne al; add rsp,28; ret`, where the stub (0xE0DC10) is retail's compiled-out `return 0`. The
 * image's xchg/lea/jmp filler after the `ret` makes the pattern unique. Its thirteen callers are the
 * whole loading-cinematic presentation (ui_stage update, steps 30/33/34/36 and the outro).
 */
constexpr std::string_view kSuppressedSignatureText =
    "48 83 EC 28 E8 ? ? ? ? 84 C0 0F 95 C0 48 83 C4 28 C3 48 87 2C 24 48 8D 64 24 08 FF 64 24 F8";
/** Compiled pattern bytes of the signature text above. */
constexpr auto kSuppressedSignature =
    signature<signature_length(kSuppressedSignatureText)>(kSuppressedSignatureText);

using Suppressed = bool(__fastcall*)() noexcept;

std::atomic<Suppressed> g_suppressedOriginal{nullptr};
hooking::detour::Handle g_suppressedHandle{};

/** Answers "suppressed" only while the ending handoff's transition window is armed. */
bool __fastcall loading_cinematics_suppressed() noexcept {
    if (omega_activity_handoff::suppress_active() || client::activity::mission_launch::suppress_loading() || state::activity::gateway_intro::suppress_loading()
        || state::activity::vanilla::homecoming::prologue::suppress_loading()) {
        return true;
    }
    const Suppressed original = g_suppressedOriginal.load(std::memory_order_acquire);
    return original != nullptr && original();
}

/** Detours the suppression wrapper. A miss only costs the ending its loading-screen presentation. */
void install_cinematic_suppression() noexcept {
    std::byte* const target =
        scan_main_image_unique(kSuppressedSignature, "loading_cinematics_suppressed");
    if (target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=cinematic_suppression result=fail reason=target");
        return;
    }
    const hooking::detour::Spec spec{target, reinterpret_cast<void*>(&loading_cinematics_suppressed)};
    if (!hooking::detour::install(spec, g_suppressedHandle)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=cinematic_suppression result=fail reason=attach");
        return;
    }
    hooking::publish_original(g_suppressedOriginal,
                              reinterpret_cast<Suppressed>(g_suppressedHandle.original));
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=cinematic_suppression result=ok");
}

/** Removes the suppression detour. */
void uninstall_cinematic_suppression() noexcept {
    if (!g_suppressedHandle.attached) {
        return;
    }
    if (hooking::detour::uninstall(g_suppressedHandle)) {
        g_suppressedOriginal.store(nullptr, std::memory_order_release);
    }
}

/**
 * The current boot-flow step accessor, `BootFlow_GetStep_NoBubbleArg`* @ `0x7FF742AED510`.
 * Decrypts the manager global and returns `mgr + 912`, or -1 when it is null. Only the call's
 * displacement is wildcarded; the `mgr + 912` field offset makes the pattern unique.
 */
constexpr std::string_view kStepSignatureText =
    "48 83 EC 28 E8 ? ? ? ? 48 85 C0 74 0B 8B 80 90 03 00 00 48 83 C4 28 C3 83 C8 FF 48 83 C4 28 "
    "C3";
/** Compiled pattern bytes of the signature text above. */
constexpr auto kStepSignature = signature<signature_length(kStepSignatureText)>(kStepSignatureText);

/** First step that loads the map with no player in it yet. */
constexpr std::int32_t kActivityLoadFirst = 33;
/** `activity:in_world`. The fade is armed by then, so a spawn now releases it. */
constexpr std::int32_t kInWorld = 38;

using GetStep = std::int64_t(__fastcall*)() noexcept;

std::atomic<GetStep> g_step{nullptr};

} // namespace

/** Publishes the client's own boot-flow step. */
void poll_world_step() noexcept {
    const graphics::hijacked_frame_timing::PostSpan timing(graphics::hijacked_frame_timing::Kind::world_step);
    // Spawning can finish before step 38, after which Destiny no longer calls the spawn gate.
    // Keep arrival observation and its pending fade completion alive on the camera frame.
    state::activity::newlight::launchpad::poll_native_objects();
    // The reset is latched by the selected Adieu run. Refresh outside the
    // mission/account locks to preserve the BAP -> mission lock order.
    if(state::activity::vanilla::adieu::prepare_starting_loadout())
        static_cast<void>(server::bap::publish_external_account_mutation());
    poll_opening_fade();
    poll_spawn_arrival();
    g_publishedStep.store(read_step(), std::memory_order_relaxed);
    g_publishedTick.store(GetTickCount64(), std::memory_order_release);
    // The sole Type-31 owner is global to its proved BA6/A9 schema gate. Queue maintenance is not
    // conditioned on the current forced destination, so a destination change cannot strand it.
    sample_type31_objective_capture();
    // The camera owner invokes this poll every frame. Keep the native HUD call on that game thread;
    // calling it from the embedded server's keepalive worker is not safe.
    sample_omega_directive_presentation();
    state::activity::vanilla::one_au::poll_escape_ship();
    state::activity::vanilla::homecoming::poll_bazaar_door();
    omega_activity_handoff::poll();
    launchpad_handoff::poll();
    hijacked_placements::poll();
    forest_candy_drops::poll();
    native_round_player_probe::poll();
    poll_native_population_admissions();
    client::activity::mission_launch::poll();
}

/** Reports whether the player is in a loaded destination. */
bool in_world() noexcept {
    if (g_publishedStep.load(std::memory_order_relaxed) != kInWorld) {
        return false;
    }
    const std::uint64_t published = g_publishedTick.load(std::memory_order_acquire);
    return published != 0 && GetTickCount64() - published < kStepStaleMs;
}

/** Maps the client's own boot-flow step onto the world phase. */
void observe_world_step() noexcept {
    const GetStep read = g_step.load(std::memory_order_acquire);
    if (read == nullptr) {
        return;
    }
    const auto step = static_cast<std::int32_t>(read() & 0xFFFFFFFF);
    state::activity::WorldPhase phase = state::activity::WorldPhase::idle;
    if (step == kInWorld) {
        phase = state::activity::WorldPhase::arrived;
    } else if (step >= kActivityLoadFirst && step < kInWorld) {
        phase = state::activity::WorldPhase::transitioning;
    } else {
        // Off a destination, so the next load is a fresh arming and logs its own release line.
        rearm_fade_release();
    }
    state::activity::note_world_phase(phase);
}

/** Finds the boot-flow step accessor. */
bool install_world_step() noexcept {
    std::byte* const target = scan_main_image_unique(kStepSignature, "bootflow_current_step");
    if (target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=world_step result=fail reason=target");
        return false;
    }
    g_step.store(reinterpret_cast<GetStep>(target), std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=world_step result=ok");
    install_cinematic_suppression();
    return true;
}

/** Clears the boot-flow step accessor it found and removes the suppression detour. */
void uninstall_world_step() noexcept {
    uninstall_cinematic_suppression();
    g_step.store(nullptr, std::memory_order_release);
}

} // namespace dawn::client::hooks::bootflow
