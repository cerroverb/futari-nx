// futari-linux (2026-10-31): ported from espgaluda2-recomp src/game_setup.cpp (GPL-3.0), for Futari's files.
#include "futari_game_setup.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fmt/format.h>
#include <imgui.h>

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/windowed_app_context.h>

extern char** environ;

namespace futari::game_setup {
namespace {

namespace fs = std::filesystem;

constexpr const char* kTitle = "Game setup";

// Futari's disc: default.xex and the game cores in media/Module.
bool HasGameFiles(const fs::path& folder) {
  std::error_code ec;
  return fs::is_regular_file(folder / "default.xex", ec) && fs::is_directory(folder / "media" / "Module", ec);
}

bool HasIsoExtension(const fs::path& path) {
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext == ".iso";
}

void CollectIsos(const fs::path& folder, std::vector<fs::path>& isos) {
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(folder, ec)) {
    if (HasIsoExtension(entry.path()) && entry.is_regular_file(ec) && entry.file_size(ec) > 0) {
      isos.push_back(entry.path());
    }
  }
}

uint64_t FolderBytes(const fs::path& folder) {
  uint64_t total = 0;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(folder, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    std::error_code size_ec;
    if (it->is_regular_file(size_ec)) {
      total += it->file_size(size_ec);
    }
  }
  return total;
}

}  // namespace

std::optional<fs::path> FindGameFolder(const fs::path& game_root) {
  if (HasGameFiles(game_root)) {
    return game_root;
  }
  std::optional<fs::path> found;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(game_root, ec)) {
    if (entry.is_directory(ec) && HasGameFiles(entry.path())) {
      if (found) {
        return std::nullopt;  // ambiguous
      }
      found = entry.path();
    }
  }
  return found;
}

struct IsoInstaller::State {
  std::atomic<bool> active{true};
  rex::ui::WindowedAppContext* context = nullptr;
  std::mutex mutex;
  std::string message = "Looking for the game files...";
  std::thread worker;
  pid_t child = -1;
};

namespace {

class ProgressDialog final : public rex::ui::ImGuiDialog {
 public:
  ProgressDialog(rex::ui::ImGuiDrawer* drawer, std::weak_ptr<IsoInstaller::State> state)
      : ImGuiDialog(drawer), state_(std::move(state)) {}

  void Finish() { Close(); }

 protected:
  void OnDraw(ImGuiIO& io) override {
    // Draw into the SDK's ImGui context (io's), whichever context is current in this module's ImGui (reNut
    // overlay_ui.cpp ContextScope).
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(io.Ctx);
    DrawPopup(io);
    ImGui::SetCurrentContext(previous);
  }

 private:
  void DrawPopup(ImGuiIO& io) {
    std::string message;
    if (auto state = state_.lock()) {
      std::lock_guard lock(state->mutex);
      message = state->message;
    }
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f));
    if (!opened_) {
      ImGui::OpenPopup(kTitle);
      opened_ = true;
    }
    if (ImGui::BeginPopupModal(kTitle, nullptr,
                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextWrapped("%s", message.c_str());
      ImGui::Spacing();
      ImGui::TextDisabled("This only happens on the first launch.");
      ImGui::EndPopup();
    } else {
      ImGui::OpenPopup(kTitle);
    }
  }

 private:
  std::weak_ptr<IsoInstaller::State> state_;
  bool opened_ = false;
};

void SetMessage(IsoInstaller::State& state, std::string message) {
  std::lock_guard lock(state.mutex);
  state.message = std::move(message);
}

// Runs extract-xiso and waits for it, updating the progress text. Returns the
// exit status, or -1 if it could not be started or was cancelled.
int RunExtractor(IsoInstaller::State& state, const fs::path& extractor, const fs::path& iso,
                 const fs::path& out_dir, const fs::path& log_path) {
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log_path.c_str(),
                                   O_WRONLY | O_CREAT | O_TRUNC, 0644);
  posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

  // -s skips the $SystemUpdate folder.
  std::string exe = extractor.string(), out = out_dir.string(), in = iso.string();
  char* argv[] = {exe.data(), const_cast<char*>("-x"), const_cast<char*>("-s"),
                  const_cast<char*>("-d"), out.data(), in.data(), nullptr};
  pid_t child = -1;
  int err = posix_spawn(&child, exe.c_str(), &actions, nullptr, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  if (err != 0) {
    return -1;
  }
  {
    std::lock_guard lock(state.mutex);
    state.child = child;
  }

  const auto name = iso.filename().string();
  auto last_poll = std::chrono::steady_clock::time_point{};
  int status = 0;
  while (true) {
    pid_t r = waitpid(child, &status, WNOHANG);
    if (r == child || (r < 0 && errno != EINTR)) {
      break;
    }
    if (!state.active.load()) {
      kill(child, SIGKILL);
      waitpid(child, &status, 0);
      return -1;
    }
    auto now = std::chrono::steady_clock::now();
    if (now - last_poll > std::chrono::seconds(1)) {
      last_poll = now;
      SetMessage(state, fmt::format("Extracting {}\n{:.2f} GB written", name,
                                    FolderBytes(out_dir) / 1e9));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  {
    std::lock_guard lock(state.mutex);
    state.child = -1;
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string Install(IsoInstaller::State& state, const fs::path& exe_dir, const fs::path& game_root) {
  std::vector<fs::path> isos;
  CollectIsos(exe_dir, isos);
  CollectIsos(game_root, isos);
  if (isos.empty()) {
    return "The game files were not found.\n\n"
           "Put your Mushihimesama Futari Ver 1.5 ISO next to futari.sh, or copy the "
           "extracted game files (default.xex and the media folder) into the game_root folder.";
  }
  if (isos.size() > 1) {
    return "More than one .iso file was found. Leave only the Mushihimesama Futari ISO next to futari.sh.";
  }
  const fs::path iso = isos.front();

  const fs::path extractor = exe_dir / "tools" / "extract-xiso" / "extract-xiso";
  std::error_code ec;
  if (!fs::is_regular_file(extractor, ec)) {
    return "tools/extract-xiso/extract-xiso is missing. Unpack the release again.";
  }
  fs::permissions(extractor, fs::perms::owner_exec, fs::perm_options::add, ec);

  // Extract into a staging folder beside game_root, then move it into place.
  fs::create_directories(game_root, ec);
  const fs::path staging = game_root.parent_path() / "game_root.extracting";
  fs::remove_all(staging, ec);
  fs::create_directories(staging, ec);
  if (ec) {
    return "Could not create " + staging.string() + ": " + ec.message();
  }
  const fs::path log_path = game_root.parent_path() / "extract-xiso.log";

  int result = RunExtractor(state, extractor, iso, staging, log_path);
  if (!state.active.load()) {
    fs::remove_all(staging, ec);
    return "Cancelled.";
  }
  if (result != 0) {
    return fmt::format("Extracting {} failed (exit code {}). See {} for details.",
                       iso.filename().string(), result, log_path.string());
  }
  if (!HasGameFiles(staging)) {
    fs::remove_all(staging, ec);
    return iso.filename().string() +
           " does not look like Mushihimesama Futari (default.xex or media/Module is missing).";
  }

  SetMessage(state, "Finishing...");
  for (const auto& entry : fs::directory_iterator(staging, ec)) {
    std::error_code move_ec;
    const fs::path target = game_root / entry.path().filename();
    if (fs::exists(target, move_ec)) {
      continue;  // keep anything the user already put there
    }
    fs::rename(entry.path(), target, move_ec);
    if (move_ec) {
      return "Could not move the extracted files into game_root: " + move_ec.message();
    }
  }
  fs::remove_all(staging, ec);
  fs::remove(log_path, ec);
  return {};
}

}  // namespace

IsoInstaller::IsoInstaller() = default;

IsoInstaller::~IsoInstaller() { Stop(); }

void IsoInstaller::Start(rex::ui::WindowedAppContext& context, rex::ui::ImGuiDrawer* drawer,
                         const fs::path& exe_dir, const fs::path& game_root, Done done) {
  if (state_) {
    return;
  }
  state_ = std::make_shared<State>();
  state_->context = &context;

  ProgressDialog* dialog = drawer ? new ProgressDialog(drawer, state_) : nullptr;
  std::weak_ptr<State> weak = state_;

  state_->worker = std::thread([state = state_.get(), weak, dialog, exe_dir, game_root,
                                done = std::move(done)]() mutable {
    std::string error;
    try {
      error = Install(*state, exe_dir, game_root);
    } catch (const std::exception& e) {
      error = e.what();
    }
    if (!state->active.load()) {
      return;
    }
    state->context->CallInUIThreadDeferred(
        [weak, dialog, error = std::move(error), done = std::move(done)] {
          auto current = weak.lock();
          if (!current || !current->active.load()) {
            return;
          }
          if (dialog) {
            dialog->Finish();
          }
          done(error.empty(), error);
        });
  });
}

void IsoInstaller::Stop() {
  if (!state_) {
    return;
  }
  state_->active.store(false);
  if (state_->worker.joinable()) {
    state_->worker.join();
  }
  state_.reset();
}

}  // namespace futari::game_setup
