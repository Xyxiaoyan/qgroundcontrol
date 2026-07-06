@echo off
setlocal enabledelayedexpansion

rem ============================================================================
rem  QGroundControl Windows Build Script
rem  Uses Visual Studio 2022 MSVC + Qt 6.10.3 + CMake/Ninja
rem ============================================================================

rem --- Fix proxy: correct malformed URL (http://http:// -> http://) ---
set "HTTP_PROXY=http://127.0.0.1:2334"
set "HTTPS_PROXY=http://127.0.0.1:2334"
set "http_proxy=http://127.0.0.1:2334"
set "https_proxy=http://127.0.0.1:2334"
set "NO_PROXY=localhost,127.0.0.1,*.local,pypi.tuna.tsinghua.edu.cn"
set "PIP_INDEX_URL=https://pypi.tuna.tsinghua.edu.cn/simple"
set "PIP_TRUSTED_HOST=pypi.tuna.tsinghua.edu.cn"
echo [OK] Proxy set to http://127.0.0.1:2334, pip mirror: Tsinghua

rem --- Git (required by CMake FetchContent/CPM) ---
rem Use junction C:\git to avoid spaces in path (CPM/ExternalProject issue)
set "GIT_DIR=C:\git\cmd"
if not exist "%GIT_DIR%\git.exe" set "GIT_DIR=C:\Program Files\Git\cmd"
if exist "%GIT_DIR%\git.exe" (
    set "PATH=%GIT_DIR%;%PATH%"
    echo [OK] Git added to PATH
) else (
    echo [WARN] Git not found
)

rem --- VS 2022 Developer Environment ---
set "VS_ROOT=C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
set "VCVARS=%VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found: %VCVARS%
    exit /b 1
)
call "%VCVARS%" >nul 2>&1
echo [OK] VS 2022 Developer Environment loaded

rem --- VS bundled CMake & Ninja ---
set "VS_CMAKE=%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "VS_NINJA=%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
set "PATH=%VS_CMAKE%;%VS_NINJA%;%PATH%"
echo [OK] CMake: & cmake --version | findstr /R "cmake version"
echo [OK] Ninja: & ninja --version

rem --- Qt Root ---
set "QT_ROOT_DIR=C:\Qt\6.10.3\msvc2022_64"
if not exist "%QT_ROOT_DIR%\lib\cmake\Qt6\qt.toolchain.cmake" (
    echo [ERROR] Qt toolchain not found at %QT_ROOT_DIR%
    exit /b 1
)
echo [OK] Qt: %QT_ROOT_DIR%

rem --- Source Directory ---
set "SRC_DIR=C:\dev\qgc-win"
if not exist "%SRC_DIR%\CMakeLists.txt" (
    echo [ERROR] Source not found: %SRC_DIR%
    exit /b 1
)
cd /d "%SRC_DIR%"
echo [OK] Source: %SRC_DIR%

rem --- Parse argument ---
set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=configure"

rem --- Configure ---
if /i "%ACTION%"=="configure" (
    echo.
    echo ======== CMake Configure ========
    cmake --preset Windows -B "%USERPROFILE%\qgc-build" -DQGC_USE_CACHE=OFF -DGIT_EXECUTABLE="C:/git/cmd/git.exe"
    if errorlevel 1 (
        echo [FAILED] CMake configure failed
        exit /b 1
    )
    echo [OK] Configure complete
    goto :end
)

rem --- Build ---
if /i "%ACTION%"=="build" (
    set "CONFIG=%~2"
    if "!CONFIG!"=="" set "CONFIG=Debug"
    echo.
    echo ======== CMake Build !CONFIG! ========
    cmake --build "%USERPROFILE%\qgc-build" --config !CONFIG! --parallel
    if errorlevel 1 (
        echo [FAILED] Build failed
        exit /b 1
    )
    echo [OK] Build complete: !CONFIG!
    goto :end
)

rem --- Build Release ---
if /i "%ACTION%"=="release" (
    echo.
    echo ======== CMake Build Release ========
    cmake --build "%USERPROFILE%\qgc-build" --config Release --parallel
    if errorlevel 1 (
        echo [FAILED] Release build failed
        exit /b 1
    )
    echo [OK] Release build complete
    goto :end
)

rem --- Clean ---
if /i "%ACTION%"=="clean" (
    echo ======== Cleaning ========
    if exist "%USERPROFILE%\qgc-build" rmdir /s /q "%USERPROFILE%\qgc-build"
    echo [OK] Clean complete
    goto :end
)

echo Usage: build_win.bat [configure^|build [Debug^|Release]^|release^|clean]

:end
endlocal
