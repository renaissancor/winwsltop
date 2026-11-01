@echo off
rem Usage: msvc.cmd <source, "all" or "release"> <output dir>
rem        msvc.cmd dependents <exe>
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSROOT=%%i
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if "%~1"=="dependents" (
  dumpbin /nologo /dependents "%~2"
  exit /b %errorlevel%
)
if not exist "%~2" mkdir "%~2"
set CL_FLAGS=/nologo /std:c++20 /EHsc /W4 /WX /O2 /Zi /utf-8 /Isrc /Fo"%~2\\" /Fd"%~2\\"
rem No /Zi and no /DEBUG: a debug build embeds the absolute .pdb path, which holds the builder's user name.
set REL_FLAGS=/nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /utf-8 /Isrc /Fo"%~2\\"
set SOURCES=src\main.cpp src\host\*.cpp src\gpu\*.cpp src\record\*.cpp src\wsl\*.cpp src\analysis\*.cpp src\ui\render.cpp src\ui\console.cpp src\ui\wsl_view.cpp
if "%~1"=="release" (
  cl %REL_FLAGS% /Fe"%~2\winwsltop.exe" %SOURCES% /link /DEBUG:NONE /Brepro
) else if "%~1"=="all" (
  cl %CL_FLAGS% /Fe"%~2\winwsltop.exe" %SOURCES%
) else (
  cl %CL_FLAGS% /Fe"%~2\\" "%~1"
)
