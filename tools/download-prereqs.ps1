param(
    [string]$OutputDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot "build\prereqs"
}
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

function Save-Download {
    param(
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][string]$FileName,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $outFile = Join-Path $OutputDir $FileName
    Write-Host "Downloading $Name..."
    Invoke-WebRequest -Uri $Url -OutFile $outFile -Headers @{ "User-Agent" = "SteamlessController packaging" }
    $stream = [System.IO.File]::OpenRead($outFile)
    try {
        $sha256 = [System.Security.Cryptography.SHA256]::Create()
        try {
            $hashBytes = $sha256.ComputeHash($stream)
            $hash = -join ($hashBytes | ForEach-Object { $_.ToString("X2") })
        } finally {
            $sha256.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
    [pscustomobject]@{
        name = $Name
        file = $FileName
        url = $Url
        sha256 = $hash
    }
}

function Get-GitHubReleaseAsset {
    param(
        [Parameter(Mandatory = $true)][string]$Repo,
        [Parameter(Mandatory = $true)][string]$NamePattern
    )

    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/latest" -Headers @{ "User-Agent" = "SteamlessController packaging" }
    $asset = $release.assets | Where-Object { $_.name -like $NamePattern } | Select-Object -First 1
    if (-not $asset) {
        throw "No asset matching '$NamePattern' found in latest release for $Repo."
    }

    [pscustomobject]@{
        tag = $release.tag_name
        name = $asset.name
        url = $asset.browser_download_url
    }
}

$manifest = @()
$manifest += Save-Download `
    -Name "Microsoft Visual C++ Redistributable 2015-2022 x64" `
    -Url "https://aka.ms/vs/17/release/vc_redist.x64.exe" `
    -FileName "VC_redist.x64.exe"

$usbip = Get-GitHubReleaseAsset -Repo "vadimgrn/usbip-win2" -NamePattern "USBip-*-x64.exe"
$manifest += Save-Download -Name "usbip-win2 $($usbip.tag)" -Url $usbip.url -FileName "USBip-x64.exe"

$hidHide = Get-GitHubReleaseAsset -Repo "nefarius/HidHide" -NamePattern "HidHide_*_x64.exe"
$manifest += Save-Download -Name "HidHide $($hidHide.tag)" -Url $hidHide.url -FileName "HidHide-x64.exe"

$manifestPath = Join-Path $OutputDir "prereqs.json"
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Write-Host "Prerequisites downloaded to $OutputDir"
