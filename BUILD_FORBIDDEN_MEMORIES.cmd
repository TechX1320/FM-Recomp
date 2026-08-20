@echo off
setlocal
set "MSYS2_ROOT=C:\msys64"
if not exist "%MSYS2_ROOT%\msys2_shell.cmd" (
  echo MSYS2 was not found at C:\msys64.
  echo Install MSYS2 in the default folder, then read README.md.
  pause
  exit /b 1
)

cd /d "%~dp0"
call "%MSYS2_ROOT%\msys2_shell.cmd" -defterm -no-start -mingw64 -here -c "bash ./scripts/build-windows.sh"
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" (
  echo.
  echo Forbidden Memories build failed with exit code %RESULT%.
  pause
  exit /b %RESULT%
)

echo.
echo The Windows build is ready in dist-windows.
pause
