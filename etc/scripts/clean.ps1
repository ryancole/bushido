<#
.SYNOPSIS
    Deletes every build artifact folder in the repo.

.DESCRIPTION
    Removes the directories the build tools create and .gitignore lists --
    build/, build-release/, out/, .vs/ and .cache/ -- and nothing else.
    Committed generated content (assets/music/*.wav, rendered by musicgen)
    is source as far as this repo is concerned and is left alone.

    A full clean means the next configure re-fetches every FetchContent
    dependency (Jolt, GLFW, GLM, miniaudio, ImGui, toml++, vk-bootstrap), so
    the following build is a long one. Use -WhatIf to see what would go.

.PARAMETER Path
    Repo root to clean. Defaults to the repo root above this script, so the
    script may be run from any working directory.

.EXAMPLE
    .\etc\scripts\clean.ps1 -WhatIf

.EXAMPLE
    .\etc\scripts\clean.ps1
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $Path
)

$ErrorActionPreference = 'Stop'

# This script lives under etc/scripts; the artifacts are at the repo root.
# Walk up for it rather than counting ..\..\, so moving this file again does
# not turn the clean into a no-op that reports success.
if (-not $Path) {
    $dir = $PSScriptRoot
    while ($dir) {
        if ((Test-Path (Join-Path $dir 'CMakePresets.json')) -and
            (Test-Path (Join-Path $dir 'CMakeLists.txt'))) { $Path = $dir; break }
        $dir = Split-Path $dir -Parent
    }
    if (-not $Path) {
        throw "Could not find the repo root above $PSScriptRoot. Pass -Path explicitly."
    }
}

$artifactDirs = @('build', 'build-release', 'out', '.vs', '.cache')

$targets = foreach ($name in $artifactDirs) {
    $dir = Join-Path $Path $name
    if (Test-Path $dir -PathType Container) {
        $stats = Get-ChildItem $dir -Recurse -File -Force -ErrorAction SilentlyContinue |
                 Measure-Object -Property Length -Sum
        [pscustomobject]@{
            Name  = $name
            Dir   = $dir
            Files = $stats.Count
            Bytes = [long]$stats.Sum
        }
    }
}

if (-not $targets) {
    Write-Host "Nothing to clean -- no build artifact folders in $Path." -ForegroundColor DarkGray
    return
}

foreach ($t in $targets) {
    $mb = '{0:N1}' -f ($t.Bytes / 1MB)
    Write-Host ("  {0,-14} {1,10} files  {2,9} MB" -f $t.Name, $t.Files, $mb) -ForegroundColor DarkGray
}

$freed = 0L
$failed = @()
foreach ($t in $targets) {
    if ($PSCmdlet.ShouldProcess($t.Dir, 'Remove directory recursively')) {
        try {
            Remove-Item $t.Dir -Recurse -Force
            $freed += $t.Bytes
        } catch {
            # Usually the exe is still running, or an editor is holding a file
            # under build/. Say which one rather than dying half way through.
            $failed += $t.Name
            Write-Host "  could not remove $($t.Dir): $($_.Exception.Message)" -ForegroundColor Yellow
        }
    }
}

if ($WhatIfPreference) { return }

if ($failed) {
    Write-Host "`nLeft behind: $($failed -join ', '). Close anything using them and re-run." -ForegroundColor Yellow
    exit 1
}

$total = '{0:N1}' -f ($freed / 1MB)
Write-Host "`nClean. Freed $total MB; the next configure will re-fetch the dependencies." -ForegroundColor Green
