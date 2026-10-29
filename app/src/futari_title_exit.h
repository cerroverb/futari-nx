// Exit from the idle title screen with B (any pad) or Escape (bind_title_exit), as CAVE's own PC ports allow.
//
// futari-linux (2026-10-29): ported from springah/futari_recomp (github.com/springah/futari_recomp 36902dd,
// src/main.cpp "Title-screen exit", contributed there by itemzer0, #3; BSD 3-Clause). Same game image (Ver 1.5
// default.xex), so the same title state function (sub_821CBFF0). The app's protected parts (input, window, ImGui
// drawer) come in through Host, since the logic lives outside FutariApp here.
#pragma once

#include <functional>

namespace rex::input {
class InputSystem;
}

namespace futari::title_exit {

struct Host {
  std::function<rex::input::InputSystem*()> input;      // the runtime's input system, or nullptr
  std::function<bool()> overlay_open;                    // UI thread: an SDK overlay that takes input is open
  std::function<void(std::function<void()>)> in_ui;      // runs a function on the UI thread
  std::function<void()> request_close;                   // UI thread: close as the title-bar close button does
};

// From FutariApp::OnCreateDialogs (UI thread): registers bind_title_exit and arms the title hook.
void Install(Host host);

}  // namespace futari::title_exit
