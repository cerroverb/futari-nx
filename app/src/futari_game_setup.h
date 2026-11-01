// futari-linux (2026-10-31): first-start setup, ported from espgaluda2-recomp src/game_setup.h (GPL-3.0): finds the
// game files, or extracts an ISO into game_root with the bundled extract-xiso (Linux only).

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace rex::ui {
class ImGuiDrawer;
class WindowedAppContext;
}  // namespace rex::ui

namespace futari::game_setup {

// Returns the folder that holds default.xex: game_root itself, or a single
// subfolder of it (when a whole game folder was copied in).
std::optional<std::filesystem::path> FindGameFolder(const std::filesystem::path& game_root);

class IsoInstaller {
 public:
  using Done = std::function<void(bool ok, const std::string& error)>;

  IsoInstaller();
  ~IsoInstaller();

  IsoInstaller(const IsoInstaller&) = delete;
  IsoInstaller& operator=(const IsoInstaller&) = delete;

  // Looks for exactly one .iso beside the executable or inside game_root and
  // extracts it into game_root on a worker thread, showing progress. `done`
  // runs on the UI thread.
  void Start(rex::ui::WindowedAppContext& context, rex::ui::ImGuiDrawer* drawer,
             const std::filesystem::path& exe_dir, const std::filesystem::path& game_root,
             Done done);
  void Stop();

  struct State;

 private:
  std::shared_ptr<State> state_;
};

}  // namespace futari::game_setup
