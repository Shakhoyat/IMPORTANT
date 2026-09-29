@echo off
rem Builds ThermalSense.exe with MinGW-w64 g++ (no make needed). Takes about a minute.
setlocal enabledelayedexpansion
cd /d "%~dp0"
set SRC=
for /r src %%f in (*.cpp) do set SRC=!SRC! "%%f"
echo Compiling ThermalSense...
g++ -std=c++17 -O2 -Isrc -Iinclude %SRC% src\glad.c -Llib -lglfw3 -lopengl32 -lgdi32 -static -static-libgcc -static-libstdc++ -o ThermalSense.exe
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed.
    exit /b 1
)
echo [OK] ThermalSense.exe built.
