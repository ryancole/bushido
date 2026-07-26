<#
.SYNOPSIS
    Configures and builds bushido in Release, optionally assembling the
    shippable dist folder.

.DESCRIPTION
    MSVC is not on PATH, so every cmake invocation is chained through
    vcvars64.bat -- a bare `cmake --preset` finds no `cl` and fails with
    "The CXX compiler identification is unknown". This script does that
    chaining, then verifies the two things that have silently produced an
    unshippable build before: that the cache really is Release, and that the
    packaged exe does not link the debug CRT.

.PARAMETER SteamSdkDir
    The extracted Steamworks SDK 'sdk' directory (the one holding public/ and
    redistributable_bin/). Without it the Steam transport compiles to a stub
    and the game still plays over direct IP.

.PARAMETER Package
    Also build the `package` target, assembling build-release/dist -- the
    folder to zip. The build directory itself is not that: it is hundreds of
    megabytes of intermediates whose exe leans on absolute paths.

.PARAMETER Fresh
    Delete build-release/ first, so the configure starts from no cache at all.

.EXAMPLE
    .\etc\scripts\build-release.ps1

.EXAMPLE
    .\etc\scripts\build-release.ps1 -Package -SteamSdkDir C:\sdk
#>
[CmdletBinding()]
param(
    [string] $SteamSdkDir,
    [switch] $Package,
    [switch] $Fresh
)

$ErrorActionPreference = 'Stop'

# The script lives under etc/scripts, but cmake --preset reads
# CMakePresets.json from the *working directory*, so everything below runs at
# the repo root. Found by walking up rather than by counting ..\..\, so moving
# this file again doesn't silently break it.
function Find-RepoRoot {
    $dir = $PSScriptRoot
    while ($dir) {
        if ((Test-Path (Join-Path $dir 'CMakePresets.json')) -and
            (Test-Path (Join-Path $dir 'CMakeLists.txt'))) { return $dir }
        $dir = Split-Path $dir -Parent
    }
    return $null
}

$repo = Find-RepoRoot
if (-not $repo) {
    throw "Could not find the repo root above $PSScriptRoot (no directory holding both CMakePresets.json and CMakeLists.txt)."
}

$buildDir  = Join-Path $repo 'build-release'
$preset    = 'msvc-release'
$vulkanSdk = 'C:\VulkanSDK\1.4.350.0'

# --- locate vcvars64.bat -----------------------------------------------------
function Find-Vcvars {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $root = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null | Select-Object -First 1
        if ($root) {
            $candidate = Join-Path $root 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path $candidate) { return $candidate }
        }
    }
    # vswhere is itself optional; fall back to the pinned install.
    $fallback = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
    if (Test-Path $fallback) { return $fallback }
    return $null
}

$vcvars = Find-Vcvars
if (-not $vcvars) {
    throw "Could not find vcvars64.bat. Install the MSVC v143 toolset, or edit the fallback path in this script."
}

# --- run commands inside the MSVC environment --------------------------------
# Written to a temp .cmd rather than passed as one giant `cmd /s /c` string:
# the quoting of a vcvars path plus a -D argument through two parsers is the
# kind of thing that breaks on the day you add an option with a space in it.
function Invoke-DevShell {
    param(
        [Parameter(Mandatory)][string[]] $Commands,
        [switch] $Capture
    )

    # vcvars64 chatters on both streams (it probes for vswhere on PATH and
    # complains when it is not there, harmlessly), so it is silenced and its
    # own failure reported instead.
    $lines = @(
        '@echo off',
        "call `"$vcvars`" >nul 2>&1 || (echo vcvars64.bat failed to set up the MSVC environment. & exit /b 1)",
        "set `"VULKAN_SDK=$vulkanSdk`"",
        # vcvars64 leaves the shell in its own directory, and --preset looks
        # for CMakePresets.json in the cwd either way, so this is what lets the
        # script be run from anywhere.
        "cd /d `"$repo`" || exit /b 1")
    foreach ($c in $Commands) {
        $lines += $c
        $lines += 'if errorlevel 1 exit /b 1'
    }

    $tmp = Join-Path $env:TEMP ("bushido-build-{0}.cmd" -f [guid]::NewGuid().ToString('N'))
    Set-Content -Path $tmp -Value $lines -Encoding ASCII
    try {
        if ($Capture) {
            $output = & cmd /s /c "`"$tmp`"" 2>&1
            return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
        }
        & cmd /s /c "`"$tmp`""
        return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $null }
    } finally {
        Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    }
}

# --- go ----------------------------------------------------------------------
if ($Fresh -and (Test-Path $buildDir)) {
    Write-Host "Removing $buildDir" -ForegroundColor DarkGray
    Remove-Item $buildDir -Recurse -Force
}

$configure = "cmake --preset $preset"
if ($SteamSdkDir) {
    if (-not (Test-Path (Join-Path $SteamSdkDir 'public\steam\steam_api.h'))) {
        throw "SteamSdkDir '$SteamSdkDir' does not look like the SDK's 'sdk' directory (no public\steam\steam_api.h)."
    }
    $configure += " -DSTEAM_SDK_DIR=`"$SteamSdkDir`""
    Write-Host "Steam networking: enabled ($SteamSdkDir)" -ForegroundColor DarkGray
} else {
    Write-Host "Steam networking: disabled (pass -SteamSdkDir to enable)" -ForegroundColor DarkGray
}

$targets = "cmake --build --preset $preset"
if ($Package) { $targets += " --target package" }

Write-Host "`nvcvars64: $vcvars" -ForegroundColor DarkGray
Write-Host "$configure`n$targets`n" -ForegroundColor Cyan

$result = Invoke-DevShell -Commands @($configure, $targets)
if ($result.ExitCode -ne 0) {
    Write-Host "`nBuild failed (exit $($result.ExitCode))." -ForegroundColor Red
    exit $result.ExitCode
}

# The cache is what the package target reads, and a stale Debug one builds
# `package` perfectly happily while producing an exe linked against
# MSVCP140D.dll / ucrtbased.dll -- not redistributable, and it will not start
# on a machine without Visual Studio, with no useful message.
$cache = Join-Path $buildDir 'CMakeCache.txt'
$buildType = (Select-String -Path $cache -Pattern '^CMAKE_BUILD_TYPE:\w+=(.*)$' |
              Select-Object -First 1).Matches.Groups[1].Value
if ($buildType -ne 'Release') {
    Write-Host "`nWARNING: CMAKE_BUILD_TYPE in the cache is '$buildType', not Release." -ForegroundColor Yellow
    Write-Host "Re-run with -Fresh before shipping anything from this directory." -ForegroundColor Yellow
}

if ($Package) {
    $dist = Join-Path $buildDir 'dist'
    $exe  = Join-Path $dist 'bushido.exe'

    # Two-second confirmation that the packaged exe is shippable: a `D` suffix
    # on the runtime DLLs is the tell.
    $deps = Invoke-DevShell -Commands @("dumpbin /nologo /dependents `"$exe`"") -Capture
    $debugCrt = $deps.Output | Select-String -Pattern '(MSVCP\d+D\.dll|VCRUNTIME\d+D\.dll|ucrtbased\.dll)'
    if ($debugCrt) {
        Write-Host "`nERROR: the packaged exe links the debug CRT:" -ForegroundColor Red
        $debugCrt | ForEach-Object { Write-Host "  $($_.Line.Trim())" -ForegroundColor Red }
        Write-Host "That may not be redistributed and will not start elsewhere. Re-run with -Fresh." -ForegroundColor Red
        exit 1
    }

    $bytes = (Get-ChildItem $dist -Recurse -File -Force -ErrorAction SilentlyContinue |
              Measure-Object -Property Length -Sum).Sum
    $mb = '{0:N1}' -f ($bytes / 1MB)
    Write-Host "`nPackaged $dist ($mb MB) -- release CRT confirmed. This is the folder to zip." -ForegroundColor Green
} else {
    Write-Host "`nBuilt $(Join-Path $buildDir 'bushido.exe')." -ForegroundColor Green
    Write-Host "Pass -Package to assemble the shippable dist folder." -ForegroundColor DarkGray
}
