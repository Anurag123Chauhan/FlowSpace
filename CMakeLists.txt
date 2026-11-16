cmake_minimum_required(VERSION 3.25)

project(FlowSpace
    VERSION 0.1.0
    DESCRIPTION "Independent per-monitor workspace manager for Windows"
    LANGUAGES CXX)

# --- toolchain expectations ---------------------------------------------------
# MinGW-w64 UCRT64 toolchain: g++, windres. Configure with, for example:
#   cmake -S . -B build -G Ninja ^
#     -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe ^
#     -DCMAKE_MAKE_PROGRAM=C:/msys64/ucrt64/bin/ninja.exe
# Dependencies are vendored: spdlog in external/spdlog, nlohmann/json in external/nlohmann.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# We do not use C++20/23 modules; scanning generates rule names with '$' that break
# MinGW's ninja lexer.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(NOT WIN32)
    message(FATAL_ERROR "FlowSpace only builds on Windows")
endif()
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    message(WARNING "FlowSpace is developed and tested with MinGW g++; other toolchains may need flag tweaks")
endif()

# Windows 10+ SDK surface; Models.h also guards _WIN32_WINNT for direct g++ builds.
add_compile_definitions(
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000000
    NOMINMAX
    $<$<CONFIG:Debug>:FLOWSPACE_DEBUG>
)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# Link statically so the exe runs without a MinGW runtime DLL.
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

# Vendored dependencies.
set(FLOWSPACE_SPDLOG_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/external/spdlog/include")
set(FLOWSPACE_JSON_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/external")

function(flowspace_apply_deps target)
    target_include_directories(${target} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${FLOWSPACE_SPDLOG_INCLUDE}"
        "${FLOWSPACE_JSON_INCLUDE}")
endfunction()

# ------------------------------------------------------------------------------
# 1. Core: domain logic, persistence, recovery journal (no Win32 UI dependencies)
# ------------------------------------------------------------------------------
add_library(flowspace_core STATIC
    src/core/WorkspaceManager.cpp
    src/core/ConfigService.cpp
    src/core/RecoveryManager.cpp
    src/core/StringUtils.cpp)
flowspace_apply_deps(flowspace_core)

# ------------------------------------------------------------------------------
# 2. Platform: Win32 API and COM interop
# ------------------------------------------------------------------------------
add_library(flowspace_platform STATIC
    src/platform/MonitorManager.cpp
    src/platform/WindowManager.cpp
    src/platform/ProcessInfo.cpp
    src/platform/WallpaperManager.cpp
    src/platform/HotkeyManager.cpp
    src/platform/StartupManager.cpp
    src/platform/WinEventHook.cpp)
flowspace_apply_deps(flowspace_platform)
target_link_libraries(flowspace_platform PUBLIC flowspace_core
    # System import libraries (propagated to every consumer of the platform layer).
    user32 gdi32 shell32 advapi32 ole32 oleaut32
    comdlg32 comctl32 msimg32 uuid
    dwmapi shcore uxtheme)

# ------------------------------------------------------------------------------
# 3. Engine: workspace orchestration
# ------------------------------------------------------------------------------
add_library(flowspace_engine STATIC
    src/engine/WorkspaceEngine.cpp)
flowspace_apply_deps(flowspace_engine)
target_link_libraries(flowspace_engine PUBLIC flowspace_core flowspace_platform)

# ------------------------------------------------------------------------------
# 4. UI: overlay, tray, switcher, settings window
# ------------------------------------------------------------------------------
add_library(flowspace_ui STATIC
    src/ui/Theme.cpp
    src/ui/Overlay.cpp
    src/ui/TrayIcon.cpp
    src/ui/SwitcherPopup.cpp
    src/ui/MainWindow.cpp)
flowspace_apply_deps(flowspace_ui)
target_link_libraries(flowspace_ui PUBLIC flowspace_core flowspace_platform flowspace_engine)

# ------------------------------------------------------------------------------
# 5. Main application (GUI subsystem). app.rc embeds icon + PerMonitorV2 manifest;
#    CMake compiles .rc sources with windres when the MinGW bin directory is on PATH
#    or CMAKE_RC_COMPILER points at windres.exe.
# ------------------------------------------------------------------------------
if(NOT DEFINED CMAKE_RC_COMPILER)
    # Let CMake auto-discover windres.exe from PATH (MinGW bin directory).
    enable_language(RC OPTIONAL)
endif()

add_executable(flowspace WIN32
    src/app/main.cpp
    packaging/app.rc)
flowspace_apply_deps(flowspace)
target_link_libraries(flowspace PRIVATE flowspace_ui flowspace_engine flowspace_platform flowspace_core)
# WIN32 already adds -mwindows; -municode selects the wide-char entry (wWinMain).
target_link_options(flowspace PRIVATE -municode)

# ------------------------------------------------------------------------------
# 6. Diagnostic CLI (Phase-1 report; read-only, console subsystem)
# ------------------------------------------------------------------------------
add_executable(flowspace-diag
    src/app/diagnostic_main.cpp)
flowspace_apply_deps(flowspace-diag)
target_link_libraries(flowspace-diag PRIVATE flowspace_platform flowspace_core)

# ------------------------------------------------------------------------------
# 7. Core-logic tests (self-contained harness; no external test framework)
# ------------------------------------------------------------------------------
add_executable(flowspace_tests
    tests/test_core.cpp)
flowspace_apply_deps(flowspace_tests)
target_link_libraries(flowspace_tests PRIVATE flowspace_core flowspace_platform)

enable_testing()
add_test(NAME core_tests COMMAND flowspace_tests)
