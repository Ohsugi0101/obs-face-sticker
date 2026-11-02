@echo off
setlocal
cd /d "%~dp0"
echo Close OBS before installing. Approve the Windows administrator prompt.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-windows.ps1"
if errorlevel 1 (
  echo Installation failed. See the error above and QUICKSTART.txt.
) else (
  echo Installation complete. Start OBS and add the face PNG effect filter.
)
pause
