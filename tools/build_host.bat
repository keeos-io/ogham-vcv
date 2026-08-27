@echo off
REM ---------------------------------------------------------------------------
REM Ogham for VCV Rack - host build (MSVC)
REM
REM Builds the firmware's DSP sources off-target against the shim in src/shim.
REM PORTABILITY CHECK ONLY - not a parity reference: MSVC's libm differs from
REM mingw's by about a last place, so its renders do not hash-match the plugin's.
REM Use tools/host.mk (GCC or clang) for anything that compares output.
REM
REM Compiles the firmware units plus whatever target is named on the command
REM line. No Rack, no libDaisy.
REM
REM   tools\build_host.bat tests\parity\render.cpp build_host\render.exe
REM
REM With no arguments it compiles the firmware units only, as a portability
REM check: if this fails, the plugin cannot be built at all.
REM ---------------------------------------------------------------------------
setlocal

set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
  echo ERROR: vcvars64.bat not found at %VCVARS%
  exit /b 1
)
call %VCVARS% >nul || exit /b 1

set ROOT=%~dp0..
set FW=%ROOT%\ogham\firmware\src
set OUT=%ROOT%\build_host
if not exist "%OUT%" mkdir "%OUT%"

REM src\shim MUST come first: it is where daisy_seed.h is found.
set INC=/I"%ROOT%\src\shim" /I"%FW%" /I"%ROOT%\dep\daisysp" /I"%ROOT%\dep\daisysp\Utility"

REM The seven firmware translation units compiled verbatim, plus DaisySP.
set FWSRC="%FW%\formulas.cpp" "%FW%\bytebeat_engine.cpp" "%FW%\bpm_clock.cpp" ^
 "%FW%\ogham_audio_pipeline.cpp" "%FW%\ogham_cv_output.cpp" "%FW%\ogham_display.cpp" ^
 "%FW%\tm1637.cpp"
set DEPSRC="%ROOT%\dep\daisysp\Effects\chorus.cpp" "%ROOT%\dep\daisysp\Effects\flanger.cpp" ^
 "%ROOT%\dep\daisysp\Effects\phaser.cpp"
set SHIMSRC="%ROOT%\src\shim\ogham_clock.cpp"

set FLAGS=/nologo /O2 /EHsc /std:c++17 /W3 /D_CRT_SECURE_NO_WARNINGS

if "%~1"=="" (
  echo Compiling firmware sources against the shim...
  cl %FLAGS% /c %INC% /Fo:"%OUT%\\" %FWSRC% %DEPSRC% %SHIMSRC%
  if errorlevel 1 exit /b 1
  echo OK - all units compile unmodified.
  exit /b 0
)

set TARGET=%~1
set EXE=%~2
if "%EXE%"=="" set EXE=%OUT%\a.exe

echo Building %EXE% ...
cl %FLAGS% %INC% /Fo:"%OUT%\\" /Fe:"%ROOT%\%EXE%" "%ROOT%\%TARGET%" %FWSRC% %DEPSRC% %SHIMSRC%
if errorlevel 1 exit /b 1
echo OK - %EXE%
exit /b 0
