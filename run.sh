#!/usr/bin/env bash
# Runs ThermalSense (builds it first if missing). Arguments are passed through.
cd "$(dirname "$0")"
[ -x ./ThermalSense ] || ./build.sh || exit 1
mode=0
for a in "$@"; do
  case "$a" in --headless|--capture|--record|--benchmark|--trace|--export-docs|--help|-h) mode=1 ;; esac
done
if [ -z "$DISPLAY$WAYLAND_DISPLAY" ] && [ "$mode" = 0 ]; then
  cat <<'MSG'
No display found (DISPLAY / WAYLAND_DISPLAY unset), so the interactive window cannot open.
Use a headless mode over SSH instead (EGL, works with the NVIDIA driver):
  ./run.sh --headless --quality epic --capture captures
  ./run.sh --headless --quality epic --benchmark build/benchmark.json
  ./run.sh --headless --quality epic --size 3840x2160 --fps 60 --encoder h264_nvenc --record report/demo_4k.mp4 60
Or start it from a desktop session (remote desktop / X forwarding). ./run.sh --help lists all options.
MSG
  exit 2
fi
exec ./ThermalSense "$@"
