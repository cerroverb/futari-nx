<div align=center>

<img src="app/futari_icon.jpg" alt="Mushihimesama Futari" width="30%">

</div>
<h1 align=center>Mushihimesama Futari for Linux</h1>

This is my fork of [ChanseyIsTheBest/futari-nx](https://github.com/ChanseyIsTheBest/futari-nx), the native Nintendo
Switch port of the Xbox 360 version of **Mushihimesama Futari Ver 1.5** (CAVE, 2009). It adds a native Linux build for
AArch64 and x86-64, with the **Black Label** and **Ver 1.01** downloadable content.

Chansey's project already handled the bulk of the work. The game's PowerPC code (`default.xex` and its eight game cores in
`media/Module/*.xex`) is statically recompiled to C++ with [ReXGlue](https://github.com/rexglue/rexglue-sdk), and
[reNut](https://github.com/masterspike52/reNut)'s Direct3D-level renderer draws the game's Direct3D calls directly.
Because that port already runs on an ARM processor, its AArch64 code carries straight over to ARM Linux handhelds. On
Linux the renderer runs on Vulkan through plume instead of deko3d. Nothing is emulated and the game runs without a
container.

I built and tested it on an ARM handheld running Armada (Adreno with Mesa Turnip), compiled inside a Fedora
toolbox and launched natively from Steam Game Mode. The x86-64 build (`linux-amd64` preset) is tested on a laptop with
an NVIDIA GPU on KDE Plasma Wayland at 240 Hz and on an AMD handheld.

The Linux changes are also offered upstream in
[ChanseyIsTheBest/futari-nx#2](https://github.com/ChanseyIsTheBest/futari-nx/pull/2). For the Switch version, use
upstream and its installer page.

> [!NOTE]
> You need your own copy of the Xbox 360 disc, either a disc image (`.iso`) or a folder you extracted it into. The Black
> Label and Ver 1.01 DLC packages (`Content/<profile>/435A07D7/00000002`) are optional.

## FAQ

### Windows/macOS?

Not supported by this fork: it is built and tested on Linux only (x86-64 and AArch64). The source still contains
upstream futari-nx's Windows PC build ([docs/building.md](docs/building.md)), untouched and untested here.
[springah/futari_recomp](https://github.com/springah/futari_recomp) builds Futari for Windows and macOS with the SDK's
emulated GPU, and upstream [futari-nx](https://github.com/ChanseyIsTheBest/futari-nx) builds for the Nintendo Switch and
Windows.

### Is this a native port?

Yes, for the game code and the graphics. The PowerPC code of `default.xex` and the eight game cores is recompiled ahead
of time into C++ and runs directly on the processor, with no CPU emulation or JIT, and reNut's Direct3D-level renderer
draws the game's Direct3D calls with Vulkan, with no Xbox 360 GPU emulation. The game mixes its own audio, and the Xbox
kernel and XAM calls are Xenia-derived reimplementations in C++.

### Is this AI?

Mostly no. The vast majority of this project is code ported and retooled from other recompilation projects, such as
futari-nx (the Switch port this is built on), reNut (the renderer and its Wayland window), UnleashedRecomp (frame
pacing, window icon), springah's futari_recomp (exit from the title screen) and SDOJ-Recomp (a reference for the
input-lag work), made to work for Futari on Linux. LLM assistance was used in the porting, but under strict supervision and guidelines and with all changes rigorously reviewed and tested. As an example of what I mean by "assisted", I developed the Linux-side input latency code myself, then ran it through Claude to improve, debug, fed it back to me to review, tweak and test, and so on.

### Does this port reduce input lag?

Yes, it is the main goal of this fork, and it is on by default: the game reads the pad as late in its frame as it
safely can, its vertical blank follows the display's refresh, and vsync is off by default. On a laptop with an NVIDIA
GPU on KDE Plasma Wayland at 240 Hz I measured 4.7 to 6.3 ms on average from pad read to present, with vsync off.
The details are under "Input latency" below.

### What does futari_vblank_display_sync do?

It is on by default, and it is not vsync. Vsync (`renut_vsync`, off by default) decides whether a finished picture
waits for the display's next refresh. `futari_vblank_display_sync` decides when the game starts each frame: the game's
60 Hz vertical blank (where it reads the pad and starts the frame) follows the display's real refresh instead of a
timer of its own that drifts against the display. The refresh is worked out from when pictures appear on screen with
Vulkan's `VK_KHR_present_wait`. A display at a whole multiple of 60 Hz (60, 120, 180, 240 Hz) is followed. At any
other rate, without present wait, or where the times do not follow the display (a desktop's Xwayland), the game keeps
its 60 Hz timer and the log says so. The game still runs at 60 frames a second either way. On a 60 Hz display the
vertical blank also waits for the previous picture to be shown (`futari_vblank_wait_shown`), so no picture stays
queued after a stall. The 4.7 to 6.3 ms above was measured with it on.

### Can this help with other CAVE games?

It already has: [Espgaluda II recomp](https://github.com/cerroverb/espgaluda2-recomp) uses this renderer port and its
input-lag work. Other CAVE games on the Xbox 360 could start from it too, alongside
[SDOJ-Recomp](https://github.com/eandis/SDOJ-Recomp).

## Installing

1. Open the installer page, **https://cerroverb.github.io/futari-linux-installer/**
2. Choose your disc image or extracted folder (with the DLC files if you have them), pick the processor of the device
   you will play on (ARM64 or x86-64), and press **Create futari-linux.zip**. Everything runs in your browser and the
   game files never leave your computer. The page checks your copy, downloads the Linux build and makes the game's
   shaders from your own executables, the way upstream's installer does for the Switch.
3. Extract the zip wherever you keep games and start `futari.sh` in the `futari-linux` folder. To play from Steam, add
   `futari.sh` as a non-Steam game and start it from Game Mode.

The release build carries no shader of the game. It reads `vulkan_spirv.bin`, which the page makes from your copy,
from its own folder. To build everything yourself instead, see below.

## Building

The whole process, with the packages for Arch Linux ARM and Fedora, is in
[docs/building-linux.md](docs/building-linux.md). Build on the machine that will run it, since the code generator and
the shader tools run on the host during the build.

From a disc image

```sh
tools/linux/build.sh --iso "/path/to/Mushihimesama Futari.iso"
tools/linux/package.sh
```

From a folder you already extracted the disc into (`default.xex` and `media/` at its top)

```sh
tools/linux/build.sh --game-dir "/path/to/the/folder"
tools/linux/package.sh
```

Codegen takes about an hour and every step is skipped when its output already exists, so the script can be run again
after a failure. Each generated file can take 1 to 2 GB to compile, so on a handheld pass `--jobs 4` or `--jobs 8` if
the build gets killed. On AArch64 you also need DirectXShaderCompiler (`dxc`), which Fedora does not package. The docs
show how to build it once. On x86-64 the prebuilt one in the tree is used.

`package.sh` puts everything in one folder (`out/futari-nx-linux-arm64/` or `out/futari-nx-linux-amd64/`)

```text
futari          the executable
*.so            the SDK runtime, the GPU plugin and the eight game cores
futari.toml     your settings
game_root/      the files of your disc
dlc/            optional Black Label and Ver 1.01, installed by the game at its first start
```

Run `./futari` from that folder. To add it to Steam, point a non-Steam game at a small script that changes into the
folder and runs `./futari`, then start it from Game Mode.

## Input latency

Low input lag was the main goal. The Switch port's work carries over (the game reads the pad as late in its frame as
it safely can, and a two-image swapchain shows the newest picture). The parts that were Switch-only got a Linux
version.

- The game's 60 Hz vblank follows the display's real refresh. Linux has no vsync event for a window, so the refresh is
  worked out from when frames actually appeared on screen, using Vulkan's `VK_KHR_present_wait`. A 60, 120, 180 or
  240 Hz display is followed. Without present wait, or at any other rate, the game keeps its 60 Hz timer and the log
  says so.
- `input_sdl_sync_poll` (on by default) updates the gamepads on the thread that reads them, right before each read.
  Before, a read got the state from the last UI-thread pump, which could be a whole frame old.

The details and the related settings are in the "Input latency" section of
[docs/building-linux.md](docs/building-linux.md).

## Settings

`futari.toml` next to the executable holds your options. Any setting can also be given on the command line as
`--name=value`.

<div align="center">

| Setting | Linux default |
| --- | --- |
| `futari_renderer` | `"renut"` |
| `renut_gpu_backend` | `"vulkan"` |
| `fullscreen` | `true` (set `false` for a window) |
| `log_level` | `"warn"` |
| `input_sdl_sync_poll` | `true` |

</div>

On a Wayland desktop (KDE Plasma 6.7 and others with the fifo-v1 protocol) the game runs as a native Wayland window;
elsewhere through X11, and under gamescope through gamescope's XWayland. `SDL_VIDEO_DRIVER=x11` forces X11. On a
screen that is not 16:9 the picture keeps its shape with black bars (`futari_fill_crop = true` fills the screen and
trims the edges instead). Press **F4** in the game for the settings window (search box, "Save to config"); every setting, with its default and what it does, is in [docs/settings.md](docs/settings.md). On the title screen, B on a pad or Escape on the keyboard (`bind_title_exit`) quits the game. On handhelds whose built-in pad is managed by InputPlumber or Steam Input, launch it from Steam
Game Mode so the controller shows up.

## What changed from upstream

- CMake presets `linux-arm64` and `linux-amd64` (clang, Ninja, Vulkan) and a Linux settings template.
- The eight game cores built as shared libraries next to the executable, loaded by file name.
- `FUTARI_RELEASE_FORMS`, on by default on Linux AArch64, so the generated code uses the same forms as the Switch
  build.
- A system `dxc` and `libdxcompiler` on AArch64 hosts, since there are no prebuilt ARM64 Linux ones.
- The display-locked vblank and the synchronous SDL pad polling described above.
- `tools/linux/build.sh`, `codegen.sh` and `package.sh`, and [docs/building-linux.md](docs/building-linux.md).
- Release builds without the game's shaders (`build.sh --release`, `package.sh --release`). They read
  `vulkan_spirv.bin` from their own folder, which the installer page makes.

The Windows and Switch builds are untouched. For the port itself (every optimisation, the renderer, audio, and how
each change was checked) see upstream's README and [docs/futari-notes.md](docs/futari-notes.md).

## Credits

- **CAVE** created Mushihimesama Futari. This is an unofficial, fan-made port with no affiliation.
- **[ChanseyIsTheBest](https://github.com/ChanseyIsTheBest/futari-nx)** made the Switch port this fork is built on.
  Everything outside the Linux changes is that work and the work it credits.
- **[Tom Clay](https://github.com/rexglue/rexglue-sdk)** made the ReXGlue SDK, built on the work of the
  **[Xenia](https://xenia.jp)** team.
- **reNut and [masterspike52](https://github.com/masterspike52/reNut)** made the Direct3D-level renderer, and
  **[plume](https://github.com/renderbag/plume)** (from UnleashedRecomp) is the graphics layer it draws with.
- **[hedge-dev](https://github.com/hedge-dev/XenosRecomp)** made XenosRecomp, the Xenos shader translator.
- **[SDOJ-Recomp](https://github.com/eandis/SDOJ-Recomp)** served as a reference for the Linux input-latency work.
- The full upstream credits and the other libraries are in upstream's README and
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- Linux port by cerroverb.

## Legal

No affiliation with CAVE or Microsoft. "Mushihimesama Futari" is a trademark of CAVE Interactive. This repository
contains no assets or program code from the original game. You must provide your own legally obtained copy, and
everything made from it (`assets/`, `app/generated/`, the shaders) is made on your own computer.

Source code is provided under the GPL-3.0 License (see [LICENSE](LICENSE)), like the ports it builds on. The SDK keeps
its BSD-3-Clause license (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
