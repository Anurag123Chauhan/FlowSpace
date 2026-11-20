param(
    [Parameter(Mandatory)]
    [ValidateSet('x64')]
    [string]$Architecture
)

$ErrorActionPreference = 'Stop'

foreach ($name in 'MINGW_BIN', 'RUNNER_TEMP', 'GITHUB_REF', 'GITHUB_SHA') {
    if (-not (Get-Item "Env:$name" -ErrorAction SilentlyContinue)) {
        throw "Required environment variable is missing: $name"
    }
}

if ($env:GITHUB_REF -match '^refs/tags/(.+)$') {
    $version = $Matches[1]
} else {
    $version = $env:GITHUB_SHA.Substring(0, 7)
}
$displayVersion = $version -replace '^v', ''
$packageName = "FlowSpace-$version-windows-$Architecture"
$packageDir = Join-Path $env:RUNNER_TEMP $packageName
$licenseDir = Join-Path $packageDir 'licenses'
New-Item -ItemType Directory -Force $licenseDir | Out-Null

Copy-Item build\cmake\flowspace.exe $packageDir
Copy-Item README.md, LICENSE, THIRD-PARTY-NOTICES.txt, UNINSTALL.md $packageDir
Copy-Item external\spdlog\LICENSE (Join-Path $licenseDir 'spdlog-LICENSE')
Copy-Item external\spdlog\include\spdlog\fmt\bundled\fmt.license.rst `
    (Join-Path $licenseDir 'fmt-LICENSE')

$pending = [System.Collections.Generic.Queue[string]]::new()
$scanned = @{}
$pending.Enqueue((Join-Path $packageDir 'flowspace.exe'))
$objdump = Join-Path $env:MINGW_BIN 'objdump.exe'
$systemDllDir = Join-Path $env:WINDIR 'System32'

while ($pending.Count -gt 0) {
    $binary = $pending.Dequeue()
    $binaryKey = $binary.ToUpperInvariant()
    if ($scanned.ContainsKey($binaryKey)) {
        continue
    }
    $scanned[$binaryKey] = $true

    $imports = @(
        & $objdump -p $binary |
            Select-String 'DLL Name:' |
            ForEach-Object { ($_ -split ':', 2)[1].Trim() } |
            Sort-Object -Unique
    )
    if ($LASTEXITCODE -ne 0) {
        throw "Could not inspect runtime dependencies of $binary"
    }

    foreach ($dll in $imports) {
        $dllUpper = $dll.ToUpperInvariant()
        if ($dllUpper.StartsWith('API-MS-WIN-') -or $dllUpper.StartsWith('EXT-MS-WIN-')) {
            continue
        }

        $packageDll = Join-Path $packageDir $dll
        if (Test-Path $packageDll) {
            $pending.Enqueue($packageDll)
            continue
        }
        if (Test-Path (Join-Path $systemDllDir $dll)) {
            continue
        }

        $runtimeDll = Join-Path $env:MINGW_BIN $dll
        if (-not (Test-Path $runtimeDll)) {
            throw "Required runtime dependency was not found: $dll (imported by $binary)"
        }
        Copy-Item $runtimeDll $packageDir
        $pending.Enqueue($packageDll)
    }
}

$requiredFiles = @(
    (Join-Path $packageDir 'flowspace.exe'),
    (Join-Path $packageDir 'README.md'),
    (Join-Path $packageDir 'LICENSE'),
    (Join-Path $packageDir 'THIRD-PARTY-NOTICES.txt'),
    (Join-Path $packageDir 'UNINSTALL.md'),
    (Join-Path $licenseDir 'spdlog-LICENSE'),
    (Join-Path $licenseDir 'fmt-LICENSE')
)
foreach ($file in $requiredFiles) {
    if (-not (Test-Path $file)) {
        throw "Required package file is missing: $file"
    }
}

$certificatePath = $null
$signTool = $null
if ($env:WINDOWS_CERTIFICATE_BASE64) {
    if (-not $env:WINDOWS_CERTIFICATE_PASSWORD) {
        throw 'WINDOWS_CERTIFICATE_PASSWORD is required when signing is enabled'
    }

    $certificatePath = Join-Path $env:RUNNER_TEMP 'flowspace-signing.pfx'
    [IO.File]::WriteAllBytes($certificatePath, [Convert]::FromBase64String($env:WINDOWS_CERTIFICATE_BASE64))
    $signTool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe' `
        -ErrorAction SilentlyContinue |
        Sort-Object FullName |
        Select-Object -Last 1
    if (-not $signTool) {
        throw 'Windows SDK signtool.exe was not found'
    }

    & $signTool.FullName sign /fd SHA256 /f $certificatePath /p $env:WINDOWS_CERTIFICATE_PASSWORD `
        /tr 'http://timestamp.digicert.com' /td SHA256 (Join-Path $packageDir 'flowspace.exe')
    if ($LASTEXITCODE -ne 0) {
        throw 'Code signing failed for flowspace.exe'
    }
} else {
    Write-Output 'Code signing skipped; certificate secrets are not configured'
}

choco install innosetup --no-progress --yes
if ($LASTEXITCODE -ne 0) {
    throw 'Inno Setup installation failed'
}
$iscc = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
if (-not (Test-Path $iscc)) {
    throw 'Inno Setup compiler was not installed'
}

$installer = Join-Path $env:RUNNER_TEMP "$packageName-setup.exe"
& $iscc "/DAppSource=$packageDir" "/DOutputDir=$env:RUNNER_TEMP" `
    "/DAppVersion=$displayVersion" "/DOutputBaseFilename=$packageName-setup" packaging\FlowSpace.iss
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $installer)) {
    throw 'Inno Setup installer creation failed'
}

if ($signTool) {
    & $signTool.FullName sign /fd SHA256 /f $certificatePath /p $env:WINDOWS_CERTIFICATE_PASSWORD `
        /tr 'http://timestamp.digicert.com' /td SHA256 $installer
    if ($LASTEXITCODE -ne 0) {
        throw 'Code signing failed for installer'
    }

    foreach ($file in @((Join-Path $packageDir 'flowspace.exe'), $installer)) {
        & $signTool.FullName verify /pa /v $file
        if ($LASTEXITCODE -ne 0) {
            throw "Signature verification failed for $file"
        }
    }
    Remove-Item $certificatePath -Force
}

$installDir = Join-Path $env:RUNNER_TEMP "$packageName-installed"
$installProcess = Start-Process -FilePath $installer -ArgumentList @(
    '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/DIR=$installDir"
) -Wait -PassThru
if ($installProcess.ExitCode -ne 0 -or -not (Test-Path (Join-Path $installDir 'flowspace.exe'))) {
    throw 'Installer smoke test failed'
}

$uninstaller = Join-Path $installDir 'unins000.exe'
if (-not (Test-Path $uninstaller)) {
    throw 'Installer smoke test could not find the uninstaller'
}
$uninstallProcess = Start-Process -FilePath $uninstaller `
    -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait -PassThru
if ($uninstallProcess.ExitCode -ne 0 -or (Test-Path $installDir)) {
    throw 'Uninstaller smoke test failed'
}

$archive = Join-Path $env:RUNNER_TEMP "$packageName.zip"
Compress-Archive -Path (Join-Path $packageDir '*') -DestinationPath $archive
$verifyDir = Join-Path $env:RUNNER_TEMP "$packageName-verify"
Expand-Archive -Path $archive -DestinationPath $verifyDir -Force
foreach ($file in @(
    'flowspace.exe',
    'README.md',
    'LICENSE',
    'THIRD-PARTY-NOTICES.txt',
    'UNINSTALL.md',
    'licenses\spdlog-LICENSE',
    'licenses\fmt-LICENSE'
)) {
    if (-not (Test-Path (Join-Path $verifyDir $file))) {
        throw "ZIP verification failed; missing: $file"
    }
}

if (-not (Test-Path $installer)) {
    throw 'Installer verification failed; installer is missing'
}

if ($env:GITHUB_OUTPUT) {
    "archive=$archive" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
    "installer=$installer" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
    "package_name=$packageName" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
}
