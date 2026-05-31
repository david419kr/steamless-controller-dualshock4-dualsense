param(
    [string]$OutputDir,
    [string]$SourceDir,
    [string]$RepoUrl = "https://github.com/Alia5/VIIPER.git",
    [string]$UpstreamTag = "v0.6.1",
    [string]$SteamlessVersion = "v0.6.1-steamless3"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Find-Tool {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [string[]]$Fallbacks = @()
    )

    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    foreach ($path in $Fallbacks) {
        if ($path -and (Test-Path -LiteralPath $path -PathType Leaf)) {
            return $path
        }
    }

    throw "Required tool '$Name' was not found. Install it or add it to PATH."
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @()
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$patchPath = Join-Path $repoRoot "third_party\viiper-patches\viiper-v0.6.1-ds4-compat.patch"
if (-not (Test-Path -LiteralPath $patchPath -PathType Leaf)) {
    throw "Patch file not found: $patchPath"
}

if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Join-Path $repoRoot "build\viiper-src"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot "build\viiper-patched"
}

$SourceDir = [System.IO.Path]::GetFullPath($SourceDir)
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)

$git = Find-Tool git @(
    "C:\Program Files\Git\cmd\git.exe",
    "C:\Program Files (x86)\Git\cmd\git.exe"
)
$go = Find-Tool go @(
    "C:\Program Files\Go\bin\go.exe",
    "C:\Program Files (x86)\Go\bin\go.exe",
    (Join-Path $env:USERPROFILE "go\bin\go.exe")
)

$sourceGitDir = Join-Path $SourceDir ".git"
$sourceGoMod = Join-Path $SourceDir "go.mod"
if (Test-Path -LiteralPath $sourceGitDir -PathType Container) {
    Write-Host "Using existing VIIPER source tree: $SourceDir"
} elseif (Test-Path -LiteralPath $sourceGoMod -PathType Leaf) {
    Write-Host "Using existing VIIPER source tree without git metadata: $SourceDir"
} else {
    if (Test-Path -LiteralPath $SourceDir) {
        $existingItem = Get-ChildItem -LiteralPath $SourceDir -Force | Select-Object -First 1
        if ($existingItem) {
            throw "SourceDir exists but is not a VIIPER source tree: $SourceDir"
        }
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $SourceDir) | Out-Null
    Invoke-Checked -FilePath $git -Arguments @("clone", "--branch", $UpstreamTag, "--depth", "1", $RepoUrl, $SourceDir)
}

$applyCheckOutput = & $git -C $SourceDir apply --check $patchPath 2>&1
if ($LASTEXITCODE -eq 0) {
    Invoke-Checked -FilePath $git -Arguments @("-C", $SourceDir, "apply", $patchPath)
} else {
    $reverseCheckOutput = & $git -C $SourceDir apply --reverse --check $patchPath 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "VIIPER DS4 compatibility patch is already applied."
    } else {
        Write-Host $applyCheckOutput
        Write-Host $reverseCheckOutput
        throw "Could not apply or verify VIIPER DS4 compatibility patch."
    }
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$exePath = Join-Path $OutputDir "viiper.exe"
$buildDate = (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd")
$commit = (& $git -C $SourceDir rev-parse --short HEAD 2>$null).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($commit)) {
    $commit = "unknown"
}

$oldCgo = $env:CGO_ENABLED
$env:CGO_ENABLED = "0"
try {
    Push-Location $SourceDir
    try {
        $ldflags = "-s -w -X main.Version=$SteamlessVersion -X main.Commit=$commit -X main.Date=$buildDate -X github.com/Alia5/VIIPER/internal/codegen/common.Version=$SteamlessVersion"
        Invoke-Checked -FilePath $go -Arguments @("build", "-trimpath", "-ldflags", $ldflags, "-o", $exePath, ".\cmd\viiper")
    } finally {
        Pop-Location
    }
} finally {
    $env:CGO_ENABLED = $oldCgo
}

$licenseSource = Join-Path $SourceDir "LICENSE.txt"
if (Test-Path -LiteralPath $licenseSource -PathType Leaf) {
    Copy-Item -LiteralPath $licenseSource -Destination (Join-Path $OutputDir "VIIPER-LICENSE.txt") -Force
}
Copy-Item -LiteralPath $patchPath -Destination (Join-Path $OutputDir "viiper-v0.6.1-ds4-compat.patch") -Force

$sourceNotice = @"
This viiper.exe was built for SteamlessController.

Upstream: $RepoUrl
Base tag: $UpstreamTag
Local version: $SteamlessVersion
Patch: viiper-v0.6.1-ds4-compat.patch

Rebuild:
  powershell -ExecutionPolicy Bypass -File tools\build-viiper.ps1 -OutputDir <release-dir>
"@
$sourceNotice | Set-Content -LiteralPath (Join-Path $OutputDir "VIIPER-SOURCE.txt") -Encoding UTF8

Write-Host "Built patched VIIPER sidecar: $exePath"
