@echo off
rem Desktop shortcut target (Prose\src\tools\install-shortcuts.ps1, "Soulslike").
rem 1) Build & Run: compile, import content, then play (run.ps1).
rem 2) Run: play the current build without building (tools\run.ps1).
rem Both film the game window with MindAttic.Capture into R:\MindAttic\Capture\Unreal.Soulslike.
cd /d "%~dp0"
echo.
echo   Unreal.Soulslike
echo.
echo   1) Build ^& Run
echo   2) Run
echo.
choice /c 12 /n /m "  Choose 1 or 2: "
if errorlevel 2 (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run.ps1"
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run.ps1"
)
if errorlevel 1 pause
