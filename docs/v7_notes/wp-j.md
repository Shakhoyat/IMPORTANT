# WP-J: playable builds (Windows exe via cross-compile, Linux/WSLg GLFW without sudo)

## Environment findings (2026-10-07)
- Ubuntu 26.04 on WSL2, no sudo, no pkg-config, no MinGW. `apt-get download` works (apt lists present).
- WSLg present: /mnt/wslg/.X11-unix/X0 (= /tmp/.X11-unix/X0) and /mnt/wslg/runtime-dir/wayland-0,
  but DISPLAY / WAYLAND_DISPLAY are unset in agent shells (set them: DISPLAY=:0, WAYLAND_DISPLAY=wayland-0,
  XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir).
- Runtime X11 / Wayland / GL libs are installed (libX11, Xrandr, Xcursor, Xi, Xinerama, wayland-client, xkbcommon, libGL);
  libdecor is missing (GLFW only dlopens it, optional).
- include/GLFW headers and lib/libglfw3.a are GLFW 3.5.1 (MinGW-w64 build); Ubuntu's libglfw3 is 3.4. The app only
  uses 26 glfw* functions, all present in 3.4.

## Progress
- [x] zig: `uv tool install ziglang` -> ziglang 0.16.0, executable `~/.local/bin/python-zig`
- [x] GLFW debs: `apt-get download libglfw3 libglfw3-dev` + `dpkg -x` into ~/.local/opt/sysroot
- [x] Linux interactive build (`build/ts-linux-gl`, then `./ThermalSense` via run_wsl.sh / build.sh) links
      ~/.local/opt/sysroot/usr/lib/x86_64-linux-gnu/libglfw.so.3 with an rpath. Launched under WSLg (X11 and Wayland):
      `OpenGL 4.6 (Core Profile) Mesa 26.0.8-1ubuntu0.3 | Microsoft Corporation | D3D12 (NVIDIA GeForce RTX 4070)`,
      `Quality preset: EPIC (auto-detected)`, render loop running (~87 % of one core, 8 threads) until `timeout` killed it.
      The three `libEGL warning: failed to get driver name for fd -1` lines are harmless (Mesa probing /dev/dri, which WSL lacks).
- [x] Windows exe via zig 0.16: `tools/build_windows.sh` (35 s at -j32, first run also builds zig's libc++ once).
      `file`: PE32+ executable (console) x86-64. objdump DLL imports: GDI32 KERNEL32 SHELL32 USER32 + api-ms-win-crt-* (UCRT).
      No glfw3.dll / libstdc++ / libgcc / OPENGL32 import (GLFW static, loads opengl32 at run time).
- [x] Verified on Windows through WSL interop (the exe started from the Linux shell runs natively on Windows):
      `./ThermalSense.exe --trace build/wpj-wincap/trace.csv 2 --size 640x360` ->
      `OpenGL 3.3.0 NVIDIA 591.86 | NVIDIA Corporation | NVIDIA GeForce RTX 4070/PCIe/SSE2`, `Quality preset: EPIC`.
      `--capture build/wpj-wincap --only cine_low_angle --size 640x360` wrote a correct PNG.
      Caution: `timeout` on an interop process kills it but loses its buffered stdout; use a self-terminating mode (--trace N).
- [x] CMake (`uvx --from cmake cmake`, 4.4.4) finds the sysroot glfw3Config via the new CMAKE_PREFIX_PATH entry and builds.

## Exact working commands
```
# Windows exe (from repo root, no sudo)
export PATH=$HOME/.local/bin:$PATH
uv tool install ziglang                       # -> ~/.local/bin/python-zig (zig 0.16.0)
tools/build_windows.sh                        # = make -j OS=Windows_NT \
#   CXX="python-zig c++ -target x86_64-windows-gnu -Wno-nullability-completeness" CC="python-zig cc -target x86_64-windows-gnu" \
#   OBJ=build/obj-win APP=ThermalSense.exe TESTS=build/tests-win.exe \
#   LDFLAGS="lib/libglfw3.a -lopengl32 -lgdi32 -luser32 -lshell32 -static" TEST_LDFLAGS="-static" ThermalSense.exe
tools/build_windows.sh --tests                # also build/tests-win.exe (run: build/tests-win.exe out.json, works via interop)
file ThermalSense.exe; objdump -p ThermalSense.exe | grep "DLL Name"

# Linux / WSLg interactive (no sudo)
mkdir -p ~/.local/opt/sysroot && cd $(mktemp -d) && apt-get download libglfw3 libglfw3-dev \
  && for d in *.deb; do dpkg -x $d ~/.local/opt/sysroot; done          # what `tools/run_wsl.sh --fetch` does
make -j32 OBJ=build/obj-linux-gl APP=ThermalSense ThermalSense          # Makefile adds -L<sysroot> -lglfw -Wl,-rpath,<sysroot>
DISPLAY=:0 ./ThermalSense                                               # = tools/run_wsl.sh
```

## Files changed (J)
- Makefile: non-headless Linux branch only: if pkg-config finds no glfw3 and $(GLFW_SYSROOT)/usr/lib/x86_64-linux-gnu/libglfw.so
  exists (default GLFW_SYSROOT=~/.local/opt/sysroot), link it with an rpath. Header comment lines. HEADLESS=1 link lines unchanged
  (checked with `make -n`).
- build.sh: auto-fetch GLFW into the sysroot (tools/run_wsl.sh --fetch) when missing; accepts the sysroot as "GLFW present".
  Restored +x on build.sh / run.sh (git showed mode 100755 -> 100644).
- run.sh: no DISPLAY but WSLg socket present -> exec tools/run_wsl.sh.
- CMakeLists.txt: CMAKE_PREFIX_PATH += ~/.local/opt/sysroot/usr before find_package(glfw3).
- tools/build_windows.sh (new), tools/run_wsl.sh (new), tools/cross_build_windows.sh -> thin wrapper to build_windows.sh.
- README: two table rows + "Windows exe from Linux / WSL2" and "WSL2 without sudo" sections.
- build.bat unchanged (native MinGW route still valid).

## Needs sudo? No.
Fallback only (if apt-get download / the sysroot route ever breaks, or for a system-wide install):
```
sudo apt install build-essential pkg-config libglfw3-dev libgl1-mesa-dev
./build.sh && ./run.sh
```
(Windows: nothing; optional native toolchain = MSYS2 MinGW-w64 then build.bat.)

## Open issues / notes
- include/GLFW + lib/libglfw3.a are GLFW 3.5.1, Ubuntu's .so is 3.4: fine today (all 26 glfw* calls exist in 3.4); a future
  3.5-only call would fail the Linux link -> then build GLFW 3.5 from source into the sysroot (X11 headers via apt-get download).
- The WSLg window appears on the Windows desktop of the WSL box; over Tailscale the user needs a desktop stream
  (Parsec / Sunshine+Moonlight / RDP) to see it. ThermalSense.exe on Windows is the better way to play (native NVIDIA GL driver).
- Old exe kept as build/ThermalSense.exe.pre-wpj. ThermalSense.pdb (~21 MB) is regenerated next to the exe by zig/lld.
- Clang-only warning in the Windows build: src/scene/Combat.h:105 private field 'burnAcc_' unused (-Wunused-private-field).

## Session 2 (after API reset)
- Shared tree was mid-edit (agent L: audio/Synth.h `Cricket`, AmbientGen moved to Ambience.cpp; later
  rendering/FrameExplorerUi.inc missing). Retried until it compiled: `tools/build_windows.sh --tests` OK at 22:02 ->
  ThermalSense.exe (3.66 MB) + build/tests-win.exe.
- build/tests-win.exe on Windows (interop): copied to C:\Users\Public -> **219 tests, 0 failed**. Run from the
  \\wsl$ share (repo dir) T167 (WAV streaming writer round trip) fails: Windows file I/O over the 9P share, not a code bug.
  Run Windows tests from an NTFS folder.
- Linux ./ThermalSense (21:55 build) relaunched with tools/run_wsl.sh: D3D12 (NVIDIA GeForce RTX 4070), EPIC.
  The incremental rebuild at 22:0x failed only because of the in-progress FrameExplorer edit (FrameExplorerUi.inc missing);
  supervisor: re-run `tools/run_wsl.sh --setup` (or ./build.sh) and `tools/build_windows.sh` once G/H/I/L/M finish.
