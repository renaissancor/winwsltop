@echo off
rem Usage: msvc.cmd <source, or "all"> <output dir>
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSROOT=%%i
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%~2" mkdir "%~2"
set CL_FLAGS=/nologo /std:c++20 /EHsc /W4 /WX /O2 /Zi /utf-8 /Isrc /Fo"%~2\\" /Fd"%~2\\"
if "%~1"=="all" (
  cl %CL_FLAGS% /Fe"%~2\winwsltop.exe" src\main.cpp src\host\*.cpp src\gpu\*.cpp src\record\*.cpp src\wsl\*.cpp src\analysis\*.cpp
) else (
  cl %CL_FLAGS% /Fe"%~2\\" "%~1"
)
