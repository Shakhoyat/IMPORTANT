#!/usr/bin/env bash
# Idempotent setup for a fresh Ubuntu GPU box (e.g. RTX 4070): deps, ffmpeg, build, smoke test.
set -e
cd "$(dirname "$0")/.."
SUDO=""; [ "$(id -u)" != 0 ] && SUDO="sudo"

echo "== packages =="
if command -v apt-get >/dev/null 2>&1; then
  $SUDO apt-get update -y
  $SUDO apt-get install -y build-essential pkg-config libglfw3-dev libgl1-mesa-dev libegl1 ffmpeg
else
  echo "apt-get not found: install g++, make, pkg-config, GLFW dev, ffmpeg with your package manager."
fi

echo "== GPU =="
if command -v nvidia-smi >/dev/null 2>&1; then
  nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv
else
  echo "WARNING: nvidia-smi not found. Install the NVIDIA driver (e.g. sudo ubuntu-drivers install) and reboot."
fi
if command -v ffmpeg >/dev/null 2>&1; then
  ffmpeg -hide_banner -encoders 2>/dev/null | grep -q nvenc && echo "ffmpeg: NVENC available" \
    || echo "ffmpeg: no NVENC encoder (use --encoder libx264)"
fi

echo "== build =="
if pkg-config --exists glfw3 2>/dev/null; then make -j"$(nproc)"; else make -j"$(nproc)" HEADLESS=1; fi

echo "== smoke test =="
mkdir -p captures
./ThermalSense --headless --quality epic --capture captures --only cine_low_angle,thermal_fpv
ls -l captures

cat <<'MSG'

Next steps:
  4K60 NVENC video:  ./ThermalSense --headless --quality epic --size 3840x2160 --fps 60 --encoder h264_nvenc --record report/demo_4k.mp4 60
  Benchmark:         ./ThermalSense --headless --quality epic --benchmark build/benchmark.json
  All figures:       ./ThermalSense --headless --quality epic --capture report/figures
  If GL_RENDERER shows llvmpipe: check nvidia-smi; pick a GPU with --gpu N or TS_EGL_DEVICE=N.
MSG
