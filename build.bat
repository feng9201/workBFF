@echo off
rem WorkBFF build & package script (based on omni_station script)
rem Usage:
rem   build.bat             configure + build only (default)
rem   build.bat -p          configure + build + deploy Qt + make installer
rem   build.bat --package   same as -p
rem   build.bat -b 1        same as -p (backward compat)
rem Edit QT_ROOT to match your Qt install path
set QT_ROOT=C:\Qt\5.15.2\msvc2019_64

cd /d %~dp0

rem parse args: -p / --package / -b 1 -> enable packaging steps (3,4)
set DO_PACKAGE=0
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="-p"         set DO_PACKAGE=1
if /i "%~1"=="--package"  set DO_PACKAGE=1
if /i "%~1"=="-b" (
    if /i "%~2"=="1" set DO_PACKAGE=1
    shift
)
shift
goto parse_args
:args_done

rem clean old output (uncomment if needed)
rem rd /s /Q .\bin

rem 1) configure: workBFF preset (x64 / VS2022 / vcpkg)
rem    only packaging sets BUILD_PACKAGE=1 so CMake bumps the version
cmake --preset workBFF -DQT5_PATH=%QT_ROOT%\lib\cmake -DBUILD_PACKAGE=%DO_PACKAGE%
if errorlevel 1 exit /b 1

rem 2) build RelWithDebInfo
cmake --build build/windows-x64 --config RelWithDebInfo
if errorlevel 1 exit /b 1

rem 3,4) packaging only
if "%DO_PACKAGE%"=="1" (
    rem step3: deploy Qt runtime
    %QT_ROOT%\bin\windeployqt.exe .\bin\RelWithDebInfo\workBFF.exe
    if errorlevel 1 exit /b 1

    rem step4: make installer
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" workBFF.iss
)