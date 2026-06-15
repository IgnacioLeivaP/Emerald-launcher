@echo off
set TEMP=%USERPROFILE%\AppData\Local\Temp
set TMP=%USERPROFILE%\AppData\Local\Temp
cd /d "%~dp0"

"C:\msys64\mingw64\bin\c++.exe" ^
  -I src -I include ^
  -isystem "C:\msys64\mingw64\include\SDL2" ^
  -std=gnu++17 -O0 -c src/launcher.cpp ^
  > "%TEMP%\check_out.txt" 2> "%TEMP%\check_err.txt"

echo === launcher.cpp errors: ===
type "%TEMP%\check_err.txt"

"C:\msys64\mingw64\bin\c++.exe" ^
  -I src -I include ^
  -isystem "C:\msys64\mingw64\include\SDL2" ^
  -std=gnu++17 -O0 -c src/prefs.cpp ^
  >> "%TEMP%\check_out.txt" 2>> "%TEMP%\check_err.txt"

echo === prefs.cpp errors: ===
type "%TEMP%\check_err.txt"

"C:\msys64\mingw64\bin\cc.exe" ^
  -I src -I include ^
  -isystem "C:\msys64\mingw64\include\SDL2" ^
  -std=gnu11 -O0 -c src/renderer.c ^
  >> "%TEMP%\check_out.txt" 2>> "%TEMP%\check_err.txt"

echo === renderer.c errors: ===
type "%TEMP%\check_err.txt"
