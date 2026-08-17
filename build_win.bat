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
rem NOTE: Do NOT use GIT_DIR as variable name - it conflicts with git's own env var
set "GIT_BIN=C:\git\cmd"
if not exist "%GIT_BIN%\git.exe" set "GIT_BIN=C:\Program Files\Git\cmd"
if exist "%GIT_BIN%\git.exe" (
    set "PATH=%GIT_BIN%;%PATH%"
    set "GIT_EXECUTABLE=%GIT_BIN%\git.exe"
    echo [OK] Git added to PATH: %GIT_BIN%
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

rem --- Build Directory (inside project for easy access) ---
set "BUILD_DIR=%SRC_DIR%\build"
set "CONFIG_TYPE=Debug"
echo [OK] Build dir: %BUILD_DIR%

rem --- Parse argument ---
set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=configure"

rem --- Configure ---
if /i "%ACTION%"=="configure" (
    if /i "%~2"=="Release" set "CONFIG_TYPE=Release"
    echo.
    echo ======== CMake Configure (!CONFIG_TYPE!) ========
    cmake -G Ninja -B "%BUILD_DIR%" ^
        -DCMAKE_BUILD_TYPE=!CONFIG_TYPE! ^
        -DCMAKE_TOOLCHAIN_FILE="%QT_ROOT_DIR%\lib\cmake\Qt6\qt.toolchain.cmake" ^
        -DQGC_USE_CACHE=OFF ^
        -DGIT_EXECUTABLE="C:/git/cmd/git.exe"
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
    rem Auto-configure if build directory missing
    if not exist "%BUILD_DIR%\CMakeCache.txt" (
        echo [WARN] Build directory not configured, running configure first...
        cmake -G Ninja -B "%BUILD_DIR%" ^
            -DCMAKE_BUILD_TYPE=!CONFIG! ^
            -DCMAKE_TOOLCHAIN_FILE="%QT_ROOT_DIR%\lib\cmake\Qt6\qt.toolchain.cmake" ^
            -DQGC_USE_CACHE=OFF ^
            -DGIT_EXECUTABLE="C:/git/cmd/git.exe"
        if errorlevel 1 (
            echo [FAILED] CMake configure failed
            exit /b 1
        )
    )
    echo.
    echo ======== CMake Build !CONFIG! ========
    cmake --build "%BUILD_DIR%" --parallel
    if errorlevel 1 (
        echo [FAILED] Build failed
        exit /b 1
    )
    echo [OK] Build complete: !CONFIG!
    goto :end
)

rem --- Build Release ---
if /i "%ACTION%"=="release" (
    rem Release needs its own build directory
    set "BUILD_DIR=%SRC_DIR%\build-release"
    rem Auto-configure if build directory missing
    if not exist "%BUILD_DIR%\CMakeCache.txt" (
        echo [WARN] Release build directory not configured, running configure first...
        cmake -G Ninja -B "%BUILD_DIR%" ^
            -DCMAKE_BUILD_TYPE=Release ^
            -DCMAKE_TOOLCHAIN_FILE="%QT_ROOT_DIR%\lib\cmake\Qt6\qt.toolchain.cmake" ^
            -DQGC_USE_CACHE=OFF ^
            -DGIT_EXECUTABLE="C:/git/cmd/git.exe"
        if errorlevel 1 (
            echo [FAILED] CMake configure failed
            exit /b 1
        )
    )
    echo.
    echo ======== CMake Build Release ========
    cmake --build "%BUILD_DIR%" --parallel
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
    if exist "%SRC_DIR%\build" rmdir /s /q "%SRC_DIR%\build"
    if exist "%SRC_DIR%\build-release" rmdir /s /q "%SRC_DIR%\build-release"
    echo [OK] Clean complete
    goto :end
)

rem --- Deploy DLLs (Qt + GStreamer) ---
if /i "%ACTION%"=="deploy" (
    set "CONFIG=%~2"
    if "!CONFIG!"=="" set "CONFIG=Debug"
    if /i "!CONFIG!"=="Release" (
        set "DEPLOY_DIR=%SRC_DIR%\build-release"
    ) else (
        set "DEPLOY_DIR=%SRC_DIR%\build\Debug"
    )
    if not exist "!DEPLOY_DIR!\AgentQGC.exe" (
        echo [ERROR] AgentQGC.exe not found in !DEPLOY_DIR!
        echo Run 'build_win.bat build' first.
        exit /b 1
    )
    echo ======== Deploying DLLs to !DEPLOY_DIR! ========
    rem Qt DLLs + QML modules + plugins
    "%QT_ROOT_DIR%\bin\windeployqt.exe" --debug --qmldir "%SRC_DIR%\build\qml" "!DEPLOY_DIR!\AgentQGC.exe"
    if errorlevel 1 (
        echo [WARN] windeployqt reported warnings
    )
    rem GStreamer DLLs
    set "GS_BIN=%SRC_DIR%\.cache\CPM\gstreamer-win-x86_64-1.28.1\sdk\bin"
    if exist "!GS_BIN!\*.dll" (
        echo Copying GStreamer DLLs...
        copy /Y "!GS_BIN!\*.dll" "!DEPLOY_DIR!\" >nul 2>&1
        echo [OK] GStreamer DLLs copied
    ) else (
        echo [WARN] GStreamer DLLs not found at !GS_BIN!
    )
    echo [OK] Deploy complete: !DEPLOY_DIR!
    goto :end
)

echo Usage: build_win.bat [configure [Release]^|build [Debug]^|release^|clean^|deploy [Debug^|Release]]

:end
endlocal
