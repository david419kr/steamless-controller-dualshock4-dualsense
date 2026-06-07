param(
    [string]$OutputDir,
    [string]$SourceDir,
    [string]$RepoUrl = "https://github.com/Alia5/VIIPER.git",
    [string]$UpstreamTag = "v0.6.1",
    [string]$SteamlessVersion = "v0.6.1-steamless8"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -Scope Global -ErrorAction SilentlyContinue) {
    $global:PSNativeCommandUseErrorActionPreference = $false
}
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

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
$patchPaths = @(
    (Join-Path $repoRoot "third_party\viiper-patches\viiper-v0.6.1-ds4-compat.patch"),
    (Join-Path $repoRoot "third_party\viiper-patches\viiper-v0.6.1-dualsense.patch"),
    (Join-Path $repoRoot "third_party\viiper-patches\viiper-v0.6.1-ns2pro-core.patch"),
    (Join-Path $repoRoot "third_party\viiper-patches\viiper-v0.6.1-nspro-core.patch")
)
foreach ($patchPath in $patchPaths) {
    if (-not (Test-Path -LiteralPath $patchPath -PathType Leaf)) {
        throw "Patch file not found: $patchPath"
    }
}

function Invoke-GitApplyProbe {
    param(
        [Parameter(Mandatory = $true)][string]$GitPath,
        [Parameter(Mandatory = $true)][string]$WorkingTree,
        [Parameter(Mandatory = $true)][string]$PatchPath,
        [switch]$Reverse
    )

    $stdout = [System.IO.Path]::GetTempFileName()
    $stderr = [System.IO.Path]::GetTempFileName()
    $oldErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $args = @("-C", $WorkingTree, "apply", "--unidiff-zero", "--whitespace=nowarn")
        if ($Reverse) {
            $args += "--reverse"
        }
        $args += @("--check", $PatchPath)
        & $GitPath @args 1>$stdout 2>$stderr
        $code = $LASTEXITCODE
        $output = ""
        if (Test-Path -LiteralPath $stdout) {
            $output += Get-Content -LiteralPath $stdout -Raw -ErrorAction SilentlyContinue
        }
        if (Test-Path -LiteralPath $stderr) {
            $output += Get-Content -LiteralPath $stderr -Raw -ErrorAction SilentlyContinue
        }
        return [pscustomobject]@{
            ExitCode = $code
            Output = $output
        }
    } finally {
        $ErrorActionPreference = $oldErrorActionPreference
        Remove-Item -LiteralPath $stdout,$stderr -Force -ErrorAction SilentlyContinue
    }
}

function Test-KnownViiperPatchApplied {
    param(
        [Parameter(Mandatory = $true)][string]$PatchName,
        [Parameter(Mandatory = $true)][string]$Tree
    )

    $registry = Join-Path $Tree "internal\registry\devices.go"
    switch ($PatchName) {
        "viiper-v0.6.1-dualsense.patch" {
            return (Test-Path -LiteralPath (Join-Path $Tree "device\dualsense\device.go") -PathType Leaf) -and
                   (Select-String -LiteralPath $registry -Pattern 'github.com/Alia5/VIIPER/device/dualsense' -SimpleMatch -Quiet)
        }
        "viiper-v0.6.1-ns2pro-core.patch" {
            return (Select-String -LiteralPath $registry -Pattern 'github.com/Alia5/VIIPER/device/ns2pro' -SimpleMatch -Quiet) -and
                   (Select-String -LiteralPath (Join-Path $Tree "usb\usbdesc.go") -Pattern 'MicrosoftOS10Descriptor' -SimpleMatch -Quiet)
        }
        "viiper-v0.6.1-nspro-core.patch" {
            return Select-String -LiteralPath $registry -Pattern 'github.com/Alia5/VIIPER/device/nspro' -SimpleMatch -Quiet
        }
        default {
            return $false
        }
    }
}

$ns2ProOverlay = Join-Path $repoRoot "third_party\viiper-patches\ns2pro-overlay"
if (-not (Test-Path -LiteralPath $ns2ProOverlay -PathType Container)) {
    throw "NS2 Pro overlay not found: $ns2ProOverlay"
}
$nsProOverlay = Join-Path $repoRoot "third_party\viiper-patches\nspro-overlay"
if (-not (Test-Path -LiteralPath $nsProOverlay -PathType Container)) {
    throw "NS Pro overlay not found: $nsProOverlay"
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

foreach ($patchPath in $patchPaths) {
    $patchName = Split-Path -Leaf $patchPath
    $applyProbe = Invoke-GitApplyProbe -GitPath $git -WorkingTree $SourceDir -PatchPath $patchPath
    if ($applyProbe.ExitCode -eq 0) {
        Invoke-Checked -FilePath $git -Arguments @("-C", $SourceDir, "apply", "--unidiff-zero", "--whitespace=nowarn", $patchPath)
    } else {
        if (Test-KnownViiperPatchApplied -PatchName $patchName -Tree $SourceDir) {
            Write-Host "VIIPER patch is already applied: $patchName"
            continue
        }
        $reverseProbe = Invoke-GitApplyProbe -GitPath $git -WorkingTree $SourceDir -PatchPath $patchPath -Reverse
        if ($reverseProbe.ExitCode -eq 0) {
            Write-Host "VIIPER patch is already applied: $patchName"
        } else {
            Write-Host $applyProbe.Output
            Write-Host $reverseProbe.Output
            throw "Could not apply or verify VIIPER patch: $patchName"
        }
    }
}

Get-ChildItem -LiteralPath $ns2ProOverlay -Force |
    Copy-Item -Destination $SourceDir -Recurse -Force
Get-ChildItem -LiteralPath $nsProOverlay -Force |
    Copy-Item -Destination $SourceDir -Recurse -Force

$registryFile = Join-Path $SourceDir "internal\registry\devices.go"
if (-not (Select-String -LiteralPath $registryFile -Pattern 'github.com/Alia5/VIIPER/device/ns2pro' -SimpleMatch -Quiet)) {
    throw "NS2 Pro device registry import is missing after VIIPER patch application: $registryFile"
}
if (-not (Select-String -LiteralPath $registryFile -Pattern 'github.com/Alia5/VIIPER/device/nspro' -SimpleMatch -Quiet)) {
    throw "NS Pro device registry import is missing after VIIPER patch application: $registryFile"
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
foreach ($patchPath in $patchPaths) {
    Copy-Item -LiteralPath $patchPath -Destination (Join-Path $OutputDir (Split-Path -Leaf $patchPath)) -Force
}
$ns2ProOverlayOutput = Join-Path $OutputDir "ns2pro-overlay"
if (Test-Path -LiteralPath $ns2ProOverlayOutput) {
    Remove-Item -LiteralPath $ns2ProOverlayOutput -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $ns2ProOverlayOutput | Out-Null
Get-ChildItem -LiteralPath $ns2ProOverlay -Force |
    Copy-Item -Destination $ns2ProOverlayOutput -Recurse -Force

$nsProOverlayOutput = Join-Path $OutputDir "nspro-overlay"
if (Test-Path -LiteralPath $nsProOverlayOutput) {
    Remove-Item -LiteralPath $nsProOverlayOutput -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $nsProOverlayOutput | Out-Null
Get-ChildItem -LiteralPath $nsProOverlay -Force |
    Copy-Item -Destination $nsProOverlayOutput -Recurse -Force

$sourceNotice = @"
This viiper.exe was built for SteamlessController.

Upstream: $RepoUrl
Base tag: $UpstreamTag
Local version: $SteamlessVersion
Patches:
  viiper-v0.6.1-ds4-compat.patch
  viiper-v0.6.1-dualsense.patch
  viiper-v0.6.1-ns2pro-core.patch
  viiper-v0.6.1-nspro-core.patch
  ns2pro-overlay/
  nspro-overlay/

Rebuild:
  powershell -ExecutionPolicy Bypass -File tools\build-viiper.ps1 -OutputDir <release-dir>
"@
$sourceNotice | Set-Content -LiteralPath (Join-Path $OutputDir "VIIPER-SOURCE.txt") -Encoding UTF8

Write-Host "Built patched VIIPER sidecar: $exePath"
