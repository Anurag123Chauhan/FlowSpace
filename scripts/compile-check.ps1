# Compiles all FlowSpace sources to object files and links the diagnostic binary.
# Used as a fast compile checkpoint during development.
$ErrorActionPreference = 'Continue'
$g = 'C:\msys64\ucrt64\bin\g++.exe'
$flags = @('-std=c++23', '-g', '-O0', '-static', '-Wall', '-Wextra', '-Isrc', '-Iexternal/spdlog/include', '-Iexternal')
New-Item -ItemType Directory -Force build\obj | Out-Null
$files = @(
    'src/core/WorkspaceManager.cpp',
    'src/core/ConfigService.cpp',
    'src/core/RecoveryManager.cpp',
    'src/core/StringUtils.cpp',
    'src/platform/MonitorManager.cpp',
    'src/platform/WindowManager.cpp',
    'src/platform/ProcessInfo.cpp',
    'src/platform/WallpaperManager.cpp',
    'src/platform/HotkeyManager.cpp',
    'src/platform/StartupManager.cpp',
    'src/platform/WinEventHook.cpp',
    'src/engine/WorkspaceEngine.cpp',
    'src/ui/Theme.cpp',
    'src/ui/Overlay.cpp',
    'src/ui/TrayIcon.cpp',
    'src/ui/SwitcherPopup.cpp',
    'src/ui/MainWindow.cpp',
    'src/app/main.cpp'
)
$failed = $false
foreach ($f in $files) {
    $out = 'build/obj/' + [IO.Path]::GetFileNameWithoutExtension($f) + '.o'
    $log = & $g @flags -c $f -o $out 2>&1
    if ($LASTEXITCODE -ne 0) {
        $failed = $true
        Write-Output "=== FAILED: $f ==="
        Write-Output ($log | Out-String)
    } elseif ($log) {
        Write-Output "=== warnings: $f ==="
        Write-Output ($log | Out-String)
    }
}
if ($failed) { Write-Output 'COMPILE CHECKPOINT: FAILURES' } else { Write-Output 'COMPILE CHECKPOINT: OK' }
