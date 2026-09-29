@echo off
rem Build (if needed) and start ThermalSense. Arguments are passed through, e.g.
rem   run.bat                         windowed 1600x900 (quality auto: EPIC on a discrete GPU)
rem   run.bat --fullscreen            full screen on the primary monitor
rem   run.bat --quality epic --size 2560x1440
rem   run.bat --help                  all options. Press F1 in the window for the controls.
cd /d "%~dp0"
if not exist "%~dp0ThermalSense.exe" call "%~dp0build.bat"
if not exist "%~dp0ThermalSense.exe" exit /b 1
"%~dp0ThermalSense.exe" %*
