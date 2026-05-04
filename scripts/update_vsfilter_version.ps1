param(
    [string]$VersionHeader,
    [string]$SolutionDir,
    [string]$Platform,
    [string]$LibassHeader
)

$ErrorActionPreference = "Stop"

$versionMutex = New-Object System.Threading.Mutex($false, "Global\xy_vsfilter_update_version_in")
$versionMutexAcquired = $false
trap {
    if ($versionMutexAcquired) {
        $versionMutex.ReleaseMutex()
    }
    $versionMutex.Dispose()
    throw $_
}

if (-not $versionMutex.WaitOne([TimeSpan]::FromSeconds(60))) {
    throw "Timed out waiting for the version header update lock."
}
$versionMutexAcquired = $true

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ($SolutionDir) {
    $RepoRoot = (Resolve-Path $SolutionDir).Path
}

if (-not $VersionHeader) {
    $VersionHeader = Join-Path $RepoRoot "src\filters\transform\vsfilter\version_in.h"
}

if (-not [System.IO.Path]::IsPathRooted($VersionHeader)) {
    $VersionHeader = Join-Path (Get-Location) $VersionHeader
}

$existing = @{}
if (Test-Path $VersionHeader) {
    foreach ($line in Get-Content $VersionHeader) {
        if ($line -match '^\s*#define\s+(\S+)\s+(.+?)\s*$') {
            $existing[$Matches[1]] = $Matches[2]
        }
    }
}

function Get-ExistingNumber($name, $fallback) {
    if ($existing.ContainsKey($name) -and $existing[$name] -match '^\d+$') {
        return [int]$existing[$name]
    }
    return $fallback
}

function Get-ExistingString($name, $fallback) {
    if ($existing.ContainsKey($name) -and $existing[$name] -match '^"(.*)"$') {
        return $Matches[1]
    }
    return $fallback
}

function Invoke-Git([string[]]$arguments) {
    $output = & git -C $RepoRoot @arguments 2>$null
    if ($LASTEXITCODE -eq 0) {
        return (($output | Out-String).Trim())
    }
    return $null
}

function Add-IfNotEmpty([System.Collections.Generic.List[string]]$list, [string]$value) {
    if (-not [string]::IsNullOrWhiteSpace($value) -and -not $list.Contains($value)) {
        $list.Add($value)
    }
}

function Convert-LibassVersionMacro([string]$value) {
    $hex = $value.Trim()
    if ($hex.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        $hex = $hex.Substring(2)
    }
    $hex = $hex.PadLeft(8, "0")

    if ($hex -match '^(\d)(\d{2})(\d{2})\d{3}$') {
        return "$([int]$Matches[1]).$([int]$Matches[2]).$([int]$Matches[3])"
    }

    return $null
}

function Get-LibassVersionFromHeader([string]$path) {
    if ([string]::IsNullOrWhiteSpace($path) -or -not (Test-Path $path)) {
        return $null
    }

    $assHeaderText = Get-Content $path -Raw
    if ($assHeaderText -match '#define\s+LIBASS_VERSION_STRING\s+"([^"]+)"') {
        return $Matches[1]
    }

    if ($assHeaderText -match '#define\s+LIBASS_VERSION\s+(0x[0-9A-Fa-f]{8,})') {
        return Convert-LibassVersionMacro $Matches[1]
    }

    return $null
}

$major = Get-ExistingNumber "XY_VSFILTER_VERSION_MAJOR" 3
$minor = Get-ExistingNumber "XY_VSFILTER_VERSION_MINOR" 0
$patch = Get-ExistingNumber "XY_VSFILTER_VERSION_PATCH" 0
$commit = Get-ExistingNumber "XY_VSFILTER_VERSION_COMMIT" 0
$sha1 = Get-ExistingString "XY_VSFILTER_VERSION_COMMIT_SHA1" ""
$libassVersion = Get-ExistingString "LIBASS_VERSION_STRING" "0.17.4"

$gitSha1 = Invoke-Git @("rev-parse", "HEAD")
if ($gitSha1) {
    $sha1 = $gitSha1
}

$tag = Invoke-Git @("describe", "--tag", "--abbrev=0")
if ($tag -and $tag -match '^(\d+)\.(\d+)\.(\d+)') {
    $major = [int]$Matches[1]
    $minor = [int]$Matches[2]
    $patch = [int]$Matches[3]
}

$currentRevCount = Invoke-Git @("rev-list", "HEAD", "--count")
$baseRevCount = Invoke-Git @("rev-list", "3.0.0.4", "--count")
if ($currentRevCount -match '^\d+$' -and $baseRevCount -match '^\d+$') {
    $commit = [int]$currentRevCount - [int]$baseRevCount + 4
}

$assHeaderCandidates = New-Object 'System.Collections.Generic.List[string]'
Add-IfNotEmpty $assHeaderCandidates $LibassHeader
Add-IfNotEmpty $assHeaderCandidates (Join-Path $RepoRoot "SMP\libass\libass\ass.h")
Add-IfNotEmpty $assHeaderCandidates (Join-Path $RepoRoot "msvc\include\ass\ass.h")
Add-IfNotEmpty $assHeaderCandidates (Join-Path $RepoRoot "include\ass\ass.h")
Add-IfNotEmpty $assHeaderCandidates (Join-Path $RepoRoot "src\thirdparty\libass\libass\ass.h")

foreach ($assHeader in $assHeaderCandidates) {
    $candidateVersion = Get-LibassVersionFromHeader $assHeader
    if ($candidateVersion) {
        $libassVersion = $candidateVersion
        break
    }
}

$buildDate = Get-Date -Format "yyyy/MM/dd"
$content = @"
#define XY_VSFILTER_VERSION_MAJOR $major
#define XY_VSFILTER_VERSION_MINOR  $minor
#define XY_VSFILTER_VERSION_PATCH  $patch
#define XY_VSFILTER_VERSION_COMMIT $commit
#define XY_VSFILTER_VERSION_COMMIT_SHA1 "$sha1"

#define LIBASS_VERSION_STRING "$libassVersion"
#define BUILD_DATE_STRING "$buildDate"
"@

$versionHeaderDir = Split-Path $VersionHeader -Parent
$versionHeaderName = Split-Path $VersionHeader -Leaf
$tempVersionHeader = Join-Path $versionHeaderDir "$versionHeaderName.tmp.$PID"
Set-Content -Path $tempVersionHeader -Value $content -Encoding ASCII
Move-Item -Path $tempVersionHeader -Destination $VersionHeader -Force

$versionMutex.ReleaseMutex()
$versionMutexAcquired = $false
$versionMutex.Dispose()
