#!/bin/sh
# Builds ThermalSense.exe for Windows from Linux with the Zig toolchain (clang + MinGW-w64 headers/CRT):
#   uv tool install ziglang   (or any zig >= 0.13 on PATH as `zig`)
#   tools/cross_build_windows.sh
# GLFW is linked statically from lib/libglfw3.a (passed by path so the linker cannot pick lib/glfw3.dll);
# the result only needs system DLLs (UCRT, user32, gdi32, shell32; opengl32 is loaded by GLFW at run time).
set -e
cd "$(dirname "$0")/.."
ZIG=${ZIG:-zig}
command -v "$ZIG" >/dev/null 2>&1 || ZIG=python-zig
OUT=build/win-obj
mkdir -p "$OUT"
"$ZIG" cc -target x86_64-windows-gnu -O2 -Iinclude -c src/glad.c -o "$OUT/glad.o"
for f in src/*.cpp src/*/*.cpp; do
    echo "$f"
done | xargs -P "$(nproc)" -I{} sh -c '"$0" c++ -target x86_64-windows-gnu -std=c++17 -O2 -DNDEBUG -Isrc -Iinclude -w -c {} -o "$1/$(echo {} | tr / _).o"' "$ZIG" "$OUT"
"$ZIG" c++ -target x86_64-windows-gnu "$OUT"/*.o lib/libglfw3.a -lopengl32 -lgdi32 -luser32 -lshell32 -o ThermalSense.exe
echo "ThermalSense.exe built"
