@echo off
rem Build (if needed) and start the interactive simulation. Press F1 in the window for the controls.
cd /d "%~dp0"
if not exist "%~dp0ThermalSense.exe" call "%~dp0build.bat"
if not exist "%~dp0ThermalSense.exe" exit /b 1
"%~dp0ThermalSense.exe"
