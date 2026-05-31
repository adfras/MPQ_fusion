@echo off
setlocal

set SCRIPT_DIR=%~dp0
for %%I in ("%SCRIPT_DIR%..\..") do set PROJECT_DIR=%%~fI
set OUT_DIR=%PROJECT_DIR%\Binaries\Win64\mediapipe

if "%MEDIAPIPE_DIR%"=="" (
  echo MEDIAPIPE_DIR is not set. Example: set MEDIAPIPE_DIR=C:\path\to\mediapipe
  exit /b 1
)

if "%BAZEL_PYTHON%"=="" (
  for /f "delims=" %%P in ('where python 2^>nul') do (
    set BAZEL_PYTHON=%%P
    goto :python_set
  )
  echo Python not found in PATH. Install Python and retry.
  exit /b 1
)
:python_set
if "%PYTHON_BIN_PATH%"=="" set PYTHON_BIN_PATH=%BAZEL_PYTHON%
if "%HERMETIC_PYTHON_VERSION%"=="" (
  for /f "tokens=2 delims= " %%V in ('"%PYTHON_BIN_PATH%" --version 2^>^&1') do set PY_VER=%%V
  for /f "tokens=1,2 delims=." %%A in ("%PY_VER%") do set HERMETIC_PYTHON_VERSION=%%A.%%B
)
if "%HERMETIC_PYTHON_VERSION%"=="" set HERMETIC_PYTHON_VERSION=3.10
if "%TF_PYTHON_VERSION%"=="" set TF_PYTHON_VERSION=%HERMETIC_PYTHON_VERSION%

if "%BAZEL_SH%"=="" (
  if exist "C:\Program Files\Git\bin\bash.exe" set BAZEL_SH=C:\Program Files\Git\bin\bash.exe
)
if "%BAZEL_SH%"=="" (
  if exist "C:\Program Files\Git\usr\bin\bash.exe" set BAZEL_SH=C:\Program Files\Git\usr\bin\bash.exe
)
if "%BAZEL_SH%"=="" (
  if exist "D:\Git\bin\bash.exe" set BAZEL_SH=D:\Git\bin\bash.exe
)
if "%BAZEL_SH%"=="" (
  if exist "D:\Git\usr\bin\bash.exe" set BAZEL_SH=D:\Git\usr\bin\bash.exe
)
if "%BAZEL_SH%"=="" (
  if exist "C:\msys64\usr\bin\bash.exe" set BAZEL_SH=C:\msys64\usr\bin\bash.exe
)
if "%BAZEL_SH%"=="" (
  echo Bash not found. Install Git for Windows or MSYS2, or enable WSL.
  exit /b 1
)

if "%BAZEL_BIN%"=="" (
  for /f "delims=" %%B in ('where bazel 2^>nul') do (
    set BAZEL_BIN=%%B
    goto :bazel_set
  )
  for /f "delims=" %%B in ('where bazelisk 2^>nul') do (
    set BAZEL_BIN=%%B
    goto :bazel_set
  )
  if exist "%SCRIPT_DIR%tools\bazelisk.exe" set BAZEL_BIN=%SCRIPT_DIR%tools\bazelisk.exe
)
:bazel_set
if "%BAZEL_BIN%"=="" (
  echo Bazel not found. Install Bazel/Bazelisk or set BAZEL_BIN to full path.
  exit /b 1
)

if "%BAZEL_OUTPUT_USER_ROOT%"=="" set BAZEL_OUTPUT_USER_ROOT=D:\bzl

echo Using Python: %PYTHON_BIN_PATH%
echo HERMETIC_PYTHON_VERSION=%HERMETIC_PYTHON_VERSION%
echo Using Bash: %BAZEL_SH%
echo Using Bazel: %BAZEL_BIN%
echo Bazel output root: %BAZEL_OUTPUT_USER_ROOT%

if not exist "%MEDIAPIPE_DIR%\WORKSPACE" (
  if not exist "%MEDIAPIPE_DIR%\MODULE.bazel" (
    echo MEDIAPIPE_DIR does not look like a MediaPipe repo: %MEDIAPIPE_DIR%
    exit /b 1
  )
)

if not exist "%MEDIAPIPE_DIR%\mediapipe_wrapper" mkdir "%MEDIAPIPE_DIR%\mediapipe_wrapper"

copy /Y "%SCRIPT_DIR%BUILD.bazel" "%MEDIAPIPE_DIR%\mediapipe_wrapper\BUILD.bazel" >nul
copy /Y "%SCRIPT_DIR%ump_shared.cc" "%MEDIAPIPE_DIR%\mediapipe_wrapper\ump_shared.cc" >nul
copy /Y "%SCRIPT_DIR%ump_shared.h" "%MEDIAPIPE_DIR%\mediapipe_wrapper\ump_shared.h" >nul

set GPU_FILE=%MEDIAPIPE_DIR%\mediapipe\gpu\gpu_service.cc
if exist "%GPU_FILE%" (
  findstr /C:"ABSL_CONST_INIT const GraphService" "%GPU_FILE%" >nul
  if errorlevel 1 (
    powershell -NoProfile -Command "(Get-Content -Raw '%GPU_FILE%').Replace('const GraphService<','ABSL_CONST_INIT const GraphService<') | Set-Content -NoNewline '%GPU_FILE%'"
  )
)

set API3_FILE=%MEDIAPIPE_DIR%\mediapipe\framework\api3\calculator_context.h
if exist "%API3_FILE%" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%patch_api3.ps1" "%API3_FILE%"
)

pushd "%MEDIAPIPE_DIR%"

"%BAZEL_BIN%" --output_user_root=%BAZEL_OUTPUT_USER_ROOT% build -c opt --define MEDIAPIPE_DISABLE_GPU=1 --repo_env=HERMETIC_PYTHON_VERSION=%HERMETIC_PYTHON_VERSION% --repo_env=TF_PYTHON_VERSION=%TF_PYTHON_VERSION% --repo_env=PYTHON_BIN_PATH=%PYTHON_BIN_PATH% --action_env=PYTHON_BIN_PATH=%PYTHON_BIN_PATH% --action_env=BAZEL_SH=%BAZEL_SH% --cxxopt=/Zc:preprocessor --conlyopt=/std:c11 --conlyopt=/experimental:c11atomics //mediapipe_wrapper:ump_shared
if errorlevel 1 (
  echo Bazel build failed.
  popd
  exit /b 1
)

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

if exist "%MEDIAPIPE_DIR%\bazel-bin\mediapipe_wrapper\ump_shared.dll" (
  copy /Y "%MEDIAPIPE_DIR%\bazel-bin\mediapipe_wrapper\ump_shared.dll" "%OUT_DIR%\ump_shared.dll" >nul
)
if exist "%MEDIAPIPE_DIR%\bazel-bin\mediapipe_wrapper\ump_shared.pdb" (
  copy /Y "%MEDIAPIPE_DIR%\bazel-bin\mediapipe_wrapper\ump_shared.pdb" "%OUT_DIR%\ump_shared.pdb" >nul
)

popd

echo Wrapper build complete.
