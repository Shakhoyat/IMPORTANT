#!/usr/bin/env bash
# Interactive ThermalSense on Linux / WSL2 without sudo: window through WSLg, rendering on the NVIDIA GPU (Mesa d3d12).
#   tools/run_wsl.sh [ThermalSense options]   fetch GLFW if needed, (re)build ./ThermalSense, start it
#   tools/run_wsl.sh --setup                  only fetch GLFW into ~/.local/opt/sysroot and build ./ThermalSense
#   tools/run_wsl.sh --fetch                  only fetch GLFW (used by ./build.sh when libglfw3-dev is missing)
#   TS_WAYLAND=1 tools/run_wsl.sh             use WSLg's Wayland socket instead of X11 (no window decorations)
#   TS_NO_BUILD=1 tools/run_wsl.sh            start the existing ./ThermalSense without running make
# GLFW comes from Ubuntu's libglfw3 / libglfw3-dev packages, fetched with `apt-get download` and unpacked with
# `dpkg -x` (no root needed); the Makefile links it with an rpath. The X11 / Wayland / GL runtime libraries GLFW
# loads are the ones already installed (libx11-6, libwayland-client0, libgl1 ...).
set -euo pipefail
cd "$(dirname "$0")/.."

SYSROOT=${GLFW_SYSROOT:-$HOME/.local/opt/sysroot}
LIBDIR=$SYSROOT/usr/lib/x86_64-linux-gnu

fetch_glfw() {
  if pkg-config --exists glfw3 2>/dev/null || [ -e "$LIBDIR/libglfw.so" ]; then return 0; fi
  command -v apt-get >/dev/null 2>&1 && command -v dpkg >/dev/null 2>&1 || {
    echo "No GLFW and no apt-get/dpkg: install GLFW 3 development files (e.g. sudo dnf install glfw-devel)."; exit 1; }
  echo "Fetching libglfw3 + libglfw3-dev into $SYSROOT (apt-get download, no sudo)"
  local tmp; tmp=$(mktemp -d)
  (cd "$tmp" && apt-get download libglfw3 libglfw3-dev)
  mkdir -p "$SYSROOT"
  for deb in "$tmp"/*.deb; do dpkg -x "$deb" "$SYSROOT"; done
  rm -rf "$tmp"
  [ -e "$LIBDIR/libglfw.so" ] || { echo "GLFW unpack failed: $LIBDIR/libglfw.so missing"; exit 1; }
}

build() {
  # Own object dir so it never collides with the headless builds (build/obj-headless, build/obj-<wp>).
  make -j"$(nproc)" OBJ=build/obj-linux-gl APP=ThermalSense GLFW_SYSROOT="$SYSROOT" ThermalSense
}

SETUP_ONLY=0
[ "${1:-}" = "--fetch" ] && { fetch_glfw; exit 0; }
[ "${1:-}" = "--setup" ] && { SETUP_ONLY=1; shift; }

fetch_glfw
if [ "${TS_NO_BUILD:-0}" != 1 ] || [ ! -x ./ThermalSense ]; then
  if ! build; then
    if [ -x ./ThermalSense ] && [ "$SETUP_ONLY" = 0 ]; then
      echo "WARNING: build failed (sources mid-edit?); starting the previous ./ThermalSense"
    else
      exit 1
    fi
  fi
fi
[ "$SETUP_ONLY" = 1 ] && { echo "[OK] ./ThermalSense (GLFW from $LIBDIR)"; exit 0; }

# WSLg: the sockets exist even when the shell (ssh / Tailscale / IDE terminal) did not export the variables.
if [ -d /mnt/wslg ]; then
  export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/mnt/wslg/runtime-dir}
  if [ "${TS_WAYLAND:-0}" = 1 ]; then
    export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-/mnt/wslg/runtime-dir/wayland-0}   # absolute path: any XDG_RUNTIME_DIR
  else
    unset WAYLAND_DISPLAY   # X11 through XWayland: WSLg draws normal Windows title bars
    export DISPLAY=${DISPLAY:-:0}
  fi
fi
if [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
  echo "No display (DISPLAY / WAYLAND_DISPLAY unset and no WSLg). Use a headless mode: ./ThermalSense --headless ..."
  exit 2
fi
export LD_LIBRARY_PATH="$LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"   # rpath already set; harmless fallback
# ThermalSense itself selects the NVIDIA adapter (preferWslGpu(): GALLIUM_DRIVER=d3d12, adapter "NVIDIA").
exec ./ThermalSense "$@"
