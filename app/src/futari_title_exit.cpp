// futari-linux (2026-10-29): ported from springah/futari_recomp 36902dd, src/main.cpp "Title-screen exit" (BSD
// 3-Clause, by itemzer0, #3). See futari_title_exit.h. Changes from the original: the app's parts through Host; the
// guest load through memcpy + byte swap as futari's other hooks (this SDK has no REX_LOAD_U32); REX_HOOK_RAW as in
// native/futari_native_frame.cpp. Its on-screen hint (an ImGuiDialog) and its overlay check (imgui_internal.h) are
// left out: here the executable holds Renut's own, hidden ImGui copy, so ImGui calls from app code reach that copy,
// not the SDK's context in librexruntime (the hint crashed the UI thread in ImGui::SetNextWindowPos). An open SDK
// overlay that takes input is found with ReXApp::InteractiveOverlayOpen (the members the SDK's input-active callback
// checks) on the UI thread instead. A pad's B needs no check here: while an overlay takes input the SDK's SDL driver
// gives the game an untouched pad (sdl_input_driver.cpp GetDeviceState).

#include "futari_title_exit.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ui/keybinds.h>

namespace futari::title_exit {
namespace {

// "Idle title" is the title scene's per-frame state function that blinks PRESS START BUTTON and polls for START
// (sub_821CBFF0, installed in the scene's next-state pointer at +72). Both routes to the title -- boot, and Main Menu
// -> Exit -- run through it, and it stops running as soon as START is accepted.
constexpr uint32_t kTitleIdleState = 0x821CBFF0;
constexpr uint32_t kTitleStateOffset = 72;
// The idle state runs every frame; if it has not run for this long, the title is gone (or hidden behind another
// scene).
constexpr std::chrono::milliseconds kTitleIdleTimeout{250};

Host g_host;
std::atomic<bool> g_installed{false};
// steady_clock time of the most recent idle-title frame; 0 when not on it.
std::atomic<int64_t> g_title_idle_ns{0};
std::atomic<bool> g_exit_requested{false};
// Guest-thread only: pad buttons as of the previous idle-title frame, so B fires on a fresh press and not on a B
// still held from the menu.
std::array<uint16_t, 4> g_title_prev_buttons{};

int64_t SteadyNowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

bool TitleScreenIdle() {
  const int64_t last = g_title_idle_ns.load(std::memory_order_acquire);
  return last != 0 && SteadyNowNs() - last < std::chrono::nanoseconds(kTitleIdleTimeout).count();
}

std::string TitleExitKeyName() {
  const std::string key = rex::cvar::Query<std::string>("bind_title_exit");
  return key == "Escape" ? "Esc" : key;
}

// Close the window exactly as the title-bar close button does, so shutdown takes the SDK's normal OnClosing path.
// Safe from any thread. While an SDK overlay (F4 settings, console, ...) is open, B and Escape belong to it.
void RequestExit(std::string source) {
  g_host.in_ui([source = std::move(source)] {
    if (g_host.overlay_open()) {
      return;
    }
    if (g_exit_requested.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    REXLOG_INFO("Title screen: exit requested ({})", source);
    g_host.request_close();
  });
}

// Called on the guest thread for every idle-title frame (the hook below). still_idle is false on the frame START
// was accepted, i.e. the title is being left.
void OnTitleIdleFrame(bool still_idle) {
  if (!still_idle) {
    g_title_idle_ns.store(0, std::memory_order_release);
    return;
  }
  const int64_t now = SteadyNowNs();
  const int64_t last = g_title_idle_ns.exchange(now, std::memory_order_acq_rel);
  const bool entered = last == 0 || now - last >= std::chrono::nanoseconds(kTitleIdleTimeout).count();
  if (!g_installed.load(std::memory_order_acquire)) {
    return;
  }
  bool b_pressed = false;
  rex::input::InputSystem* input = g_host.input();
  for (uint32_t user = 0; input && user < g_title_prev_buttons.size(); ++user) {
    rex::input::X_INPUT_STATE state = {};
    const uint16_t buttons = input->GetState(user, &state) == 0 ? uint16_t(state.gamepad.buttons) : 0;
    const uint16_t newly_down = buttons & ~g_title_prev_buttons[user];
    g_title_prev_buttons[user] = buttons;
    // On the first idle frame only record what is held, so a B carried over from the menu does not quit the moment
    // the title appears. An open overlay is checked in RequestExit.
    if (!entered && (newly_down & rex::input::X_INPUT_GAMEPAD_B)) {
      b_pressed = true;
    }
  }
  if (b_pressed) {
    RequestExit("B");
  }
}

uint32_t LoadGuestU32(const uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, base + address, 4);
  return __builtin_bswap32(value);
}

}  // namespace

void Install(Host host) {
  g_host = std::move(host);
  rex::ui::RegisterBind("bind_title_exit", "Escape", "Exit the game from the title screen", [] {
    if (TitleScreenIdle()) {
      RequestExit(TitleExitKeyName());
    }
  });
  g_installed.store(true, std::memory_order_release);
}

}  // namespace futari::title_exit

// Wrap the idle-title state. Generated guest functions are weak aliases of __imp__sub_*, so this strong definition
// takes over the dispatch entry without any codegen change; the original still runs first, unmodified. Afterwards the
// scene's next-state pointer tells whether the title is still idle or START was just accepted.
REX_EXTERN(__imp__sub_821CBFF0);
REX_HOOK_RAW(sub_821CBFF0) {
  const uint32_t scene = ctx.r3.u32;
  __imp__sub_821CBFF0(ctx, base);
  futari::title_exit::OnTitleIdleFrame(futari::title_exit::LoadGuestU32(base, scene + futari::title_exit::kTitleStateOffset) ==
                                       futari::title_exit::kTitleIdleState);
}
