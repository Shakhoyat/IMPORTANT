#!/usr/bin/env bash
# Builds ThermalSense on Linux. Usage: ./build.sh [--headless] [--cmake]
set -e
cd "$(dirname "$0")"
HEADLESS=0; USE_CMAKE=0
for a in "$@"; do
  case "$a" in
    --headless) HEADLESS=1 ;;
    --cmake) USE_CMAKE=1 ;;
    *) echo "unknown option $a"; exit 1 ;;
  esac
done
have() { command -v "$1" >/dev/null 2>&1; }

if ! have g++; then
  echo "g++ not found. Install: sudo apt install build-essential pkg-config   (Fedora: sudo dnf install gcc-c++ make pkgconf)"
  exit 1
fi
if [ "$USE_CMAKE" = 1 ]; then
  have cmake || { echo "cmake not found: sudo apt install cmake"; exit 1; }
elif ! have make; then
  echo "make not found: sudo apt install build-essential"; exit 1
fi

# GLFW: system package (pkg-config glfw3), or Ubuntu's libglfw3 unpacked into ~/.local/opt/sysroot without sudo
# (tools/run_wsl.sh --fetch; the Makefile then links it with an rpath).
glfw_ok() {
  pkg-config --exists glfw3 2>/dev/null || [ -e "${GLFW_SYSROOT:-$HOME/.local/opt/sysroot}/usr/lib/x86_64-linux-gnu/libglfw.so" ]
}
if [ "$HEADLESS" = 0 ] && ! glfw_ok && have apt-get && have dpkg; then
  echo "GLFW not installed: fetching libglfw3 / libglfw3-dev into ~/.local/opt/sysroot (no sudo needed)"
  tools/run_wsl.sh --fetch || true
fi
if [ "$HEADLESS" = 0 ] && ! glfw_ok; then
  echo "GLFW 3 development files not found (pkg-config glfw3)."
  echo "  Debian/Ubuntu: sudo apt install build-essential pkg-config libglfw3-dev libgl1-mesa-dev"
  echo "  Fedora:        sudo dnf install gcc-c++ make pkgconf glfw-devel mesa-libGL-devel"
  echo "Headless NVIDIA server (no display): the NVIDIA driver already provides libEGL_nvidia; no GLFW needed."
  if [ -n "$DISPLAY$WAYLAND_DISPLAY" ]; then
    echo "A display is present, so you probably want the windowed build: install GLFW as above and re-run."
  fi
  if [ -t 0 ]; then
    read -r -p "Build the headless-only variant (HEADLESS=1) instead? [y/N] " ans
    case "$ans" in y|Y) HEADLESS=1 ;; *) exit 1 ;; esac
  else
    echo "Re-run with ./build.sh --headless for the EGL-only build."; exit 1
  fi
fi

JOBS=$(nproc 2>/dev/null || echo 4)
if [ "$USE_CMAKE" = 1 ]; then
  ARGS=""; [ "$HEADLESS" = 1 ] && ARGS="-DTS_HEADLESS_ONLY=ON"
  cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release $ARGS
  cmake --build build-cmake -j"$JOBS"
  cp build-cmake/ThermalSense ./ThermalSense
else
  make -j"$JOBS" HEADLESS=$HEADLESS
fi
echo "[OK] ./ThermalSense built$([ "$HEADLESS" = 1 ] && echo ' (headless only: use --headless)')."
