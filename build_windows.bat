@echo off
REM Builds WHALE.vst3 on Windows.
REM Requirements: Visual Studio 2022 (workload "Desktop development with C++"), CMake 3.22+, Git.
REM JUCE 8 is downloaded automatically on the first build.

cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || goto :error
cmake --build build --config Release --target WHALE_VST3 || goto :error

echo.
echo Build OK: build\WHALE_artefacts\Release\VST3\WHALE.vst3
echo Copy that folder to C:\Program Files\Common Files\VST3\ then rescan plugins in FL Studio / Ableton Live.
goto :eof

:error
echo Build failed.
exit /b 1
