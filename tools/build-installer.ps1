param(
    [string]$BuildDir,
    [string]$Configuration = "Release",
    [string]$PrereqDir,
    [string]$OutputDir,
    [switch]$SkipBuild
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

    throw "Required tool '$Name' was not found. Install Inno Setup 6 or add ISCC.exe to PATH."
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
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build\release"
}
if ([string]::IsNullOrWhiteSpace($PrereqDir)) {
    $PrereqDir = Join-Path $repoRoot "build\prereqs"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot "build\installer"
}

$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$PrereqDir = [System.IO.Path]::GetFullPath($PrereqDir)
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
$buildOutputDir = Join-Path $BuildDir $Configuration

if (-not $SkipBuild) {
    Invoke-Checked -FilePath "cmake" -Arguments @("--preset", "release")
    Invoke-Checked -FilePath "cmake" -Arguments @("--build", "--preset", "release")
    Invoke-Checked -FilePath "cmake" -Arguments @("--build", $BuildDir, "--config", $Configuration, "--target", "SteamlessControllerTests")
    Invoke-Checked -FilePath (Join-Path $buildOutputDir "SteamlessControllerTests.exe")
}

& (Join-Path $PSScriptRoot "download-prereqs.ps1") -OutputDir $PrereqDir

$requiredFiles = @(
    (Join-Path $buildOutputDir "SteamlessController.exe"),
    (Join-Path $buildOutputDir "viiper.exe"),
    (Join-Path $PrereqDir "VC_redist.x64.exe"),
    (Join-Path $PrereqDir "USBip-x64.exe"),
    (Join-Path $PrereqDir "HidHide-x64.exe")
)
foreach ($path in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required package input missing: $path"
    }
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$iscc = Find-Tool ISCC.exe @(
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe"
)

$iss = Join-Path $repoRoot "resources\InnoInstallerScript.iss"
Invoke-Checked -FilePath $iscc -Arguments @(
    $iss,
    "/DMyBuildDir=$buildOutputDir",
    "/DPrereqDir=$PrereqDir",
    "/DInstallerOutputDir=$OutputDir"
)

Write-Host "Installer output: $(Join-Path $OutputDir 'SteamlessController-Setup.exe')"
