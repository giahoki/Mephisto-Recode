@echo off
setlocal
set "mephisto_browser=%~1"
if /I "%mephisto_browser%"=="chrome" goto selected
if /I "%mephisto_browser%"=="firefox" goto selected
echo.
echo Mephisto WinUI 3 - Build
echo   1. Chrome / Edge
echo   2. Firefox / Zen
choice /C 12 /N /M "Select browser [1/2]: "
if errorlevel 2 (set "mephisto_browser=firefox") else (set "mephisto_browser=chrome")
:selected
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0native\build.ps1" -Browser %mephisto_browser%
set "mephisto_build_result=%ERRORLEVEL%"
if "%mephisto_build_result%"=="0" (echo Build ready in "%~dp0build\%mephisto_browser%") else (echo Build failed. See the error above.)
if /I not "%~2"=="--no-pause" pause
exit /b %mephisto_build_result%
