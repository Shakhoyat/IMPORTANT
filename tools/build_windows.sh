#!/usr/bin/env bash
# Cross-builds ThermalSense.exe (Windows x64) on Linux / WSL2 without sudo, using zig's clang + MinGW-w64 target.
#   tools/build_windows.sh            incremental build -> ./ThermalSense.exe   (objects in build/obj-win)
#   tools/build_windows.sh --tests    also build/tests-win.exe (CPU-only unit tests, run them on Windows)
#   tools/build_windows.sh --clean    remove build/obj-win first
# Toolchain: $ZIG, else `zig`, else `python-zig` (from `uv tool install ziglang`, installed automatically if uv exists).
# Uses the Makefile's Windows branch (OS=Windows_NT) with GLFW linked statically from lib/libglfw3.a (by path, so the
# linker can never pick lib/glfw3.dll). The exe needs only system DLLs (KERNEL32, USER32, GDI32, SHELL32, UCRT
# api-ms-win-crt-*); opengl32.dll is loaded by GLFW at run time, so the NVIDIA OpenGL driver is used on Windows.
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"

TESTS=0; CLEAN=0
for a in "$@"; do
  case "$a" in
    --tests) TESTS=1 ;;
    --clean) CLEAN=1 ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    *) echo "unknown option $a"; exit 1 ;;
  esac
done

ZIG=${ZIG:-}
if [ -z "$ZIG" ]; then
  if command -v zig >/dev/null 2>&1; then ZIG=zig
  elif command -v python-zig >/dev/null 2>&1; then ZIG=python-zig
  elif command -v uv >/dev/null 2>&1; then
    echo "zig not found: installing it with 'uv tool install ziglang' (user-local, no sudo)"
    uv tool install ziglang && ZIG=python-zig
  else
    echo "zig not found. Install uv (curl -LsSf https://astral.sh/uv/install.sh | sh) then: uv tool install ziglang"
    exit 1
  fi
fi
echo "toolchain: $ZIG $("$ZIG" version)"

TARGET=x86_64-windows-gnu
OBJ=build/obj-win
[ "$CLEAN" = 1 ] && rm -rf "$OBJ"
GOALS=ThermalSense.exe
[ "$TESTS" = 1 ] && GOALS="$GOALS build/tests-win.exe"

make -j"$(nproc)" OS=Windows_NT \
  CXX="$ZIG c++ -target $TARGET -Wno-nullability-completeness" CC="$ZIG cc -target $TARGET" \
  OBJ="$OBJ" APP=ThermalSense.exe TESTS=build/tests-win.exe \
  LDFLAGS="lib/libglfw3.a -lopengl32 -lgdi32 -luser32 -lshell32 -static" \
  TEST_LDFLAGS="-static" \
  $GOALS

file ThermalSense.exe
if command -v objdump >/dev/null 2>&1; then
  echo "DLL imports: $(objdump -p ThermalSense.exe | awk '/DLL Name:/ {print $3}' | sort -u | tr '\n' ' ')"
fi
echo "[OK] ThermalSense.exe built (copy it to Windows together with nothing else; run it with run.bat or directly)."
