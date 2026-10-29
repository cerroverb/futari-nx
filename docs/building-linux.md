# Building futari-nx for Linux (AArch64)

The Linux build is the PC build's layout on the Switch build's processor:

- The SDK's own Linux layer (SDL3 for the window, input and audio; Vulkan), as the Windows build uses it.
- The eight game cores as shared libraries next to the executable, as they are DLLs on Windows. The executable
  exports its `Futari*` entries for them (`--export-dynamic-symbol=Futari*`), which the cores find through the dynamic
  linker where Windows uses `GetProcAddress`.
- Renut's Direct3D-level renderer on plume's Vulkan back end (`renut_gpu_backend = "vulkan"`), with its game shaders
  translated at build time by Renut's XenosRecomp and DXC, as on the PC.
- The Switch's AArch64 code: the NEON forms of the generated code test `__aarch64__` (`rex/ppc/intrinsics.h`,
  `tools/vec_byteswap.py`, `tools/guest_cas.py`, `tools/pch_neon_shuffles.py`), so they apply as they are. The
  post-codegen steps' proofs compile to the Switch's forms (`FUTARI_RELEASE_FORMS`, ON by default on AArch64: no PC
  self-checks), and the game code is built without the frame pointer (`FUTARI_ARM64_OMIT_FRAME_POINTER`).

Build on the machine that runs it. The code generator, Renut's XenosRecomp and the shader steps are host programs the
build runs, so a cross build has nothing to run them with.

## What you need

- Your own Xbox 360 disc image of Mushihimesama Futari Ver 1.5 (`.iso`). The DLC is optional (below).
- Clang 18 or later with C++23 (the presets use `clang` / `clang++`), CMake 3.25 or later, Ninja, pkg-config, Git.
- Python 3 with the `xxhash` module.
- `glslangValidator` (the glslang tools).
- DirectXShaderCompiler (`dxc` and `libdxcompiler.so`). XenosRecomp's `dxc-bin` folder in this repository holds no Linux binaries, so the system's DXC is used on AArch64 and on x86-64 (Arch's `directx-shader-compiler`).
- Development files of X11 (with x11-xcb), xcb and Wayland; ALSA, PulseAudio or PipeWire (SDL3 picks the audio
  back ends it finds headers for).
- A Vulkan driver (Mesa Turnip on Adreno, Panfrost/PanVK on Mali, NVK on NVIDIA) and the Vulkan loader.

Arch Linux ARM, and Arch or CachyOS on x86-64:

```sh
sudo pacman -S --needed base-devel clang lld cmake ninja pkgconf git python python-xxhash glslang directx-shader-compiler libx11 libxcb wayland alsa-lib libpulse pipewire vulkan-icd-loader
```

Fedora (a toolbox or distrobox container on an image-based system such as Armada):

```sh
sudo dnf install clang lld cmake ninja-build pkgconf-pkg-config git python3 python3-xxhash glslang libX11-devel libxcb-devel wayland-devel alsa-lib-devel pulseaudio-libs-devel pipewire-devel vulkan-loader-devel
```

Fedora has no DirectXShaderCompiler package; build it once from source:

```sh
git clone --recurse-submodules https://github.com/microsoft/DirectXShaderCompiler.git ~/src/dxc
cmake ~/src/dxc -B ~/src/dxc-build -C ~/src/dxc/cmake/caches/PredefinedParams.cmake -DCMAKE_BUILD_TYPE=Release -G Ninja
ninja -C ~/src/dxc-build
```

Then add `~/src/dxc-build/bin` to `PATH`, or pass `-DFUTARI_DXC=$HOME/src/dxc-build/bin/dxc
-DDXC_SYSTEM_LIB=$HOME/src/dxc-build/lib/libdxcompiler.so` to the configure step.

## Build

```sh
tools/linux/build.sh --iso "/path/to/Mushihimesama Futari.iso"
tools/linux/package.sh
```

A folder the disc was already extracted into (`default.xex` and `media/` at its top) works too:
`tools/linux/build.sh --game-dir "/path/to/the/folder"`. A `dlc/` folder in it is moved to `out/dlc`, and
`package.sh` puts it next to the executable.

`build.sh` runs the steps of [building.md](building.md) in their Linux form, each skipped when its output is there:

1. `tools/fetch_thirdparty.py`, the SDK's third-party sources.
2. `tools/extract_iso.py`, your disc into `assets/game_root`.
3. The host code generator (`out/host`, target `rexglue`).
4. `tools/linux/codegen.sh --perf`: the code generator over `default.xex` and the eight cores, then
   `tools/post_codegen.py` (the Linux form of `tools\codegen.ps1 -Perf`; about an hour).
5. `tools/renut_futari_hooks.py` and `tools/post_codegen.py` again, then `tools/perf_codegen_post.py --check`.
6. `tools/renut_dump_shaders.py`, the shader containers of the nine images.
7. `cmake --preset linux-arm64 -S app` and `cmake --build out/linux-arm64`.

A tree whose `app/generated` was made on another machine (Windows, or before this port) is generated again with
`--force-codegen`: the generated pch takes the `REX_RELEASE_FORMS` macros at codegen.

`--jobs N` limits the parallel compile jobs when the device runs out of memory.

The app build runs the code generator again once, with the copy of `rexglue` it builds itself (the build re-runs
codegen whenever the generator is newer than its output); it finds nothing to change and repeats the post-codegen
steps, a few minutes.

`package.sh` makes `out/futari-nx-linux-arm64/`:

```text
futari                  the executable
*.so                    the SDK runtime, the GPU plugin and the eight game cores
futari.toml             your settings (app/futari_linux.toml)
game_root/              the files of your disc
```

Run `./futari` from that folder. The Black Label and Ver 1.01 DLC packages go in a `dlc/` folder there, as
`dlc/<package files>` or `dlc/00000002/<package name>/`; the game installs them at its next start.

## A release for the installer page

A build made with `--release` carries no translated game shader. Renut's table of numbers about the shaders stays in
the executable (`tools/linux/release_shader_table.py`, as futari-nx's `tools/renut_release_shaders.py` does for the
Switch NRO), and the shaders themselves are read from `vulkan_spirv.bin` beside the executable. The installer page
(futari-linux-installer) makes that file from the player's own executables, with Renut's translator and DXC in
WebAssembly.

```sh
tools/linux/build.sh --release
tools/linux/package.sh --release
```

`package.sh --release` makes `out/futari-linux-<aarch64|x86_64>/` and its `.tar.gz`, with the program, `futari.sh`,
the settings template and the licenses, and no game file or shader. That archive is the release asset the installer's
deployment takes. `build.sh --release` uses the same build folder, so only the shader table and the link run again;
`build.sh` without it builds the shaders in again.

To test a release build before publishing, put a `vulkan_spirv.bin` beside it. The installer's
`node test/vulkan_node.mjs <game> --out vulkan_spirv.bin` makes the same file the page does. A build with the shaders
built in writes its own in the same layout with `--renut_export_spirv_package=<file>`, for a comparison with the
native chain's shaders. Both files are made from the game and are never distributed. Without the file, the log says
`No ... vulkan_spirv.bin and no game shaders built in` and nothing of the game is drawn.

## Settings

`futari.toml` next to the executable:

| Setting | Linux default |
| --- | --- |
| `futari_renderer` | `"renut"` |
| `renut_gpu_backend` | `"vulkan"` |
| `fullscreen` | `true` |
| `log_level` | `"warn"` |

Every enhancement it does not mention is on, as on the Switch (`app/src/futari_enhancements.cpp`). The Switch-only
ones (deko3d, NVK, core pinning, clocks) do nothing here.

The Vulkan surface is made on whichever video driver SDL chose: `VK_KHR_wayland_surface` on a native Wayland window,
`VK_KHR_xlib_surface` on X11 or XWayland (plume's `PLUME_SDL_VULKAN_ENABLED` path; the surface in
`app/src/renut_engine/gpu/backend/native_window.cpp`, ported from reNut). SDL 3.5 takes Wayland when the compositor
offers `wp_fifo_manager_v1` (KWin 6.7 does) and X11 otherwise; gamescope gives the game no Wayland socket, so there it
runs through gamescope's XWayland as before. `SDL_VIDEO_DRIVER=x11` or `wayland` (or the `video_driver` setting)
forces one. The log says "Vulkan surface on Wayland" or "on X11".

## Input latency

The Switch build's input-lag work (docs/futari-notes.md, "Input lag") carries over, with the parts that were
Switch-only given a Linux form:

- `futari_late_input` (on): the frame's first controller read waits by what the frame can spare before the next
  vblank. Platform-independent; nothing changed.
- `futari_vblank_display_sync` and `futari_vblank_phase_auto` (on): the game's vertical blank locked to the
  display's refresh, as late after it as the image still makes the next refresh. The Switch takes the refresh from
  the display's vsync event. Linux has no such event for a window, so here the refreshes are found from when the
  game's images were shown, with Vulkan's `VK_KHR_present_wait` (`Video::WaitPresentShown`,
  `futari_renut_vblank.cpp` `FromPresents`). A display at 60 Hz or a whole multiple of it (120, 180, 240 Hz) is
  followed; any other rate, or a driver without present wait, keeps the 60 Hz timer, and the log says which. Under a
  desktop compositor (KWin, through XWayland) present wait reports when the compositor took each image instead of a
  refresh, and the grid would follow the game's own presents: the vblanks of every 2 s are counted, and a rate more than
  3 % away from the display's gives the vblank back to the timer (the log says so). Under gamescope (Steam's Game Mode)
  and on native Wayland (KWin 6.7, NVIDIA 615, 240 Hz) the lock holds. The
  "[latency]" line every 10 s gives the phase, the images that were shown a refresh late and the vblanks that waited for the previous image.
- `futari_vblank_wait_shown` (on): on a display of one refresh a game frame (60 Hz), an image left queued behind
  another after a stall stayed queued for good, a frame more input lag (main's late count rose 600 per 10 s for a whole
  run). The guest vblank now waits, at most a frame, until the newest image is on screen, as UnleashedRecomp starts a
  frame only once the previous image is shown; after a stall one vblank comes a refresh later and the queue is empty.
- `input_sdl_sync_poll` (new, on): the SDK's SDL input driver answered the game's controller read from the events of
  the previous UI-thread pump, so the state it returned was as old as the game's previous read. Each read now updates
  the gamepads on the reading thread first (`SDL_UpdateGamepads`, which SDL allows from any thread); reads closer
  together than `input_sdl_sync_poll_us` (500) share one update.
- `futari_renut_double_buffer` (on): a 2-image swapchain, as on the Switch.

Run it fullscreen. Under gamescope the game is presented through gamescope's XWayland. `renut_vsync = false`
presents without waiting for the refresh (Vulkan immediate mode): an image reaches the screen as soon as it is done,
with tearing.

On a screen that is not 16:9 the whole picture is shown with black bars (letterbox or pillarbox), never stretched;
`futari_fill_crop = true` fills the screen instead and trims up to 15 % of the picture's edges. The log line
"Output WxH: the game's ... image shown at ..." says where the picture goes.

## Build options

| CMake option | Default on Linux AArch64 | What it does |
| --- | --- | --- |
| `FUTARI_RELEASE_FORMS` | ON | The post-codegen steps' Switch forms. OFF: the PC's self-checking forms (slower; `[verify]` log lines). Regenerate after changing it |
| `FUTARI_ARM64_OMIT_FRAME_POINTER` | ON | Game code without the x29 frame chain |
| `FUTARI_LINUX_RELEASE_SHADERS` | OFF | ON: no translated game shader in the executable, `vulkan_spirv.bin` beside it (`build.sh --release`) |
| `FUTARI_DXC` | `dxc` on the PATH | DXC for Renut's host shaders and XenosRecomp |
| `DXC_SYSTEM_LIB` | `libdxcompiler.so` in the library folders | The DXC library XenosRecomp links |
| `FUTARI_GLSLANG_VALIDATOR` | `glslangValidator` on the PATH | For the native renderer's utility shaders |

The `linux-amd64` preset builds the same tree on x86-64 Linux. There DXC is the system's too, unless the
`bin/x64/dxc-linux` and `lib/x64/libdxcompiler.so` of [renderbag/dxc-bin](https://github.com/renderbag/dxc-bin) are
put in `app/thirdparty/reblue-XenosRecomp/thirdparty/dxc-bin` or `FUTARI_DXC` names another.

Not carried over from the Switch build: LTO, PGO and the hot-function order (GCC options and a console profile of the
NRO), and the console's thread pinning and clock settings.
