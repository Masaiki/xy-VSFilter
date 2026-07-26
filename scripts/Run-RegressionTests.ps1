[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration,

    [Parameter(Mandatory)]
    [ValidateSet('Win32', 'x64')]
    [string]$Platform,

    [string]$ResultsDirectory = '.ci-results',

    [switch]$UpdateGolden,

    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$GoldenBaselineSha
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [System.IO.Path]::GetFullPath(
    (Join-Path -Path $PSScriptRoot -ChildPath '..'))
if ([System.IO.Path]::IsPathRooted($ResultsDirectory)) {
    $resultsPath = [System.IO.Path]::GetFullPath($ResultsDirectory)
} else {
    $resultsPath = [System.IO.Path]::GetFullPath(
        (Join-Path -Path $repoRoot -ChildPath $ResultsDirectory))
}

if ($UpdateGolden -and $env:CI) {
    throw 'Golden files must not be updated from CI.'
}
if ($UpdateGolden -and -not $GoldenBaselineSha) {
    throw '-GoldenBaselineSha is required with -UpdateGolden.'
}
if ($UpdateGolden) {
    $currentSha = (& git -C $repoRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or
        $currentSha -ne $GoldenBaselineSha.ToLowerInvariant()) {
        throw "Golden baseline SHA must match checked-out HEAD ($currentSha)."
    }
}

$null = New-Item -ItemType Directory -Path $resultsPath -Force
$msbuildCommand = Get-Command -Name 'MSBuild.exe' -ErrorAction SilentlyContinue
if ($msbuildCommand) {
    $msbuildPath = $msbuildCommand.Source
} else {
    $vswherePath = Join-Path -Path ${env:ProgramFiles(x86)} `
        -ChildPath 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswherePath -PathType Leaf)) {
        throw 'MSBuild.exe is not on PATH and vswhere.exe is unavailable.'
    }
    $msbuildPath = & $vswherePath `
        -latest `
        -products '*' `
        -requires Microsoft.Component.MSBuild `
        -find 'MSBuild\**\Bin\MSBuild.exe' |
        Select-Object -First 1
    if (-not $msbuildPath) {
        throw 'No Visual Studio installation containing MSBuild was found.'
    }
}
$solutionPath = Join-Path -Path $repoRoot -ChildPath 'VSFilter.sln'
$gtestProject = Join-Path -Path $repoRoot `
    -ChildPath 'src\thirdparty\gtest\msvc\gtest.vcxproj'
$unitTestProject = Join-Path -Path $repoRoot `
    -ChildPath 'test\unit_test\unit_test.vcxproj'
$releaseProject = Join-Path -Path $repoRoot `
    -ChildPath 'src\filters\transform\vsfilter\xy_sub_filter.vcxproj'
$unitTestExe = Join-Path -Path $repoRoot -ChildPath (
    "bin\lib_17.0\$Platform\$Configuration\unit_test.exe")
$testScript = Join-Path -Path $repoRoot -ChildPath 'test\1.ass'
$gtestConfiguration = "$Configuration Unicode"

function Invoke-CheckedProcess {
    param(
        [Parameter(Mandatory)]
        [string]$FilePath,

        [Parameter(Mandatory)]
        [string[]]$Arguments,

        [Parameter(Mandatory)]
        [string]$Description
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Invoke-UnitTestPhase {
    param(
        [Parameter(Mandatory)]
        [string]$Name,

        [Parameter(Mandatory)]
        [string]$Filter,

        [ValidateRange(1, 100)]
        [int]$Repeat = 1
    )

    $stdoutPath = Join-Path -Path $resultsPath -ChildPath "$Name.stdout.log"
    $stderrPath = Join-Path -Path $resultsPath -ChildPath "$Name.stderr.log"
    $xmlPath = Join-Path -Path $resultsPath -ChildPath "$Name.xml"

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $unitTestExe
    $startInfo.WorkingDirectory = $repoRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.ArgumentList.Add($testScript)
    $startInfo.Environment['GTEST_FILTER'] = $Filter
    $startInfo.Environment['GTEST_REPEAT'] =
        $Repeat.ToString([System.Globalization.CultureInfo]::InvariantCulture)
    $startInfo.Environment['GTEST_OUTPUT'] = "xml:$xmlPath"
    $startInfo.Environment['XY_TEST_NONINTERACTIVE'] = '1'
    $startInfo.Environment['XY_TEST_RESULTS'] = $resultsPath
    if ($UpdateGolden) {
        $startInfo.Environment['XY_UPDATE_GOLDEN'] = '1'
        $startInfo.Environment['XY_GOLDEN_BASELINE_SHA'] = $GoldenBaselineSha
    } else {
        $startInfo.Environment.Remove('XY_UPDATE_GOLDEN')
        $startInfo.Environment.Remove('XY_GOLDEN_BASELINE_SHA')
    }

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Failed to start unit-test phase '$Name'."
    }

    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $completed = $process.WaitForExit(600000)
    if (-not $completed) {
        try {
            $process.Kill($true)
        } catch {
            & taskkill.exe /PID $process.Id /T /F | Out-Null
        }
        $process.WaitForExit()
    }

    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [System.IO.File]::WriteAllText(
        $stdoutPath, $stdout, [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText(
        $stderrPath, $stderr, [System.Text.UTF8Encoding]::new($false))

    if ($stdout) {
        Write-Host $stdout
    }
    if ($stderr) {
        Write-Host $stderr
    }
    if (-not $completed) {
        throw "Unit-test phase '$Name' exceeded the 600-second timeout."
    }
    if ($process.ExitCode -ne 0) {
        throw "Unit-test phase '$Name' failed with exit code $($process.ExitCode)."
    }
}

Push-Location -LiteralPath $repoRoot
try {
    $unitTestBuildLog = Join-Path -Path $resultsPath `
        -ChildPath 'unit-test-solution-build.log'
    $unitTestBuildArguments = @(
        $solutionPath,
        '/t:vsfilter:Rebuild',
        '/m',
        '/verbosity:minimal',
        '/p:WindowsTargetPlatformVersion=10.0',
        '/p:PlatformToolset=v143',
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        '/fl',
        "/flp:logfile=$unitTestBuildLog;verbosity=normal"
    )
    Invoke-CheckedProcess -FilePath $msbuildPath `
        -Arguments $unitTestBuildArguments `
        -Description 'VSFilter dependency solution target'

    $gtestBuildLog = Join-Path -Path $resultsPath `
        -ChildPath 'gtest-build.log'
    $gtestBuildArguments = @(
        $gtestProject,
        '/t:Build',
        '/m',
        '/verbosity:minimal',
        '/p:WindowsTargetPlatformVersion=10.0',
        '/p:PlatformToolset=v143',
        "/p:Configuration=$gtestConfiguration",
        "/p:Platform=$Platform",
        "/p:SolutionDir=$repoRoot\",
        '/fl',
        "/flp:logfile=$gtestBuildLog;verbosity=normal"
    )
    Invoke-CheckedProcess -FilePath $msbuildPath `
        -Arguments $gtestBuildArguments `
        -Description 'GoogleTest project'

    $unitTestProjectLog = Join-Path -Path $resultsPath `
        -ChildPath 'unit-test-project-build.log'
    $unitTestProjectArguments = @(
        $unitTestProject,
        '/t:Build',
        '/m',
        '/verbosity:minimal',
        '/p:WindowsTargetPlatformVersion=10.0',
        '/p:PlatformToolset=v143',
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        "/p:SolutionDir=$repoRoot\",
        '/fl',
        "/flp:logfile=$unitTestProjectLog;verbosity=normal"
    )
    Invoke-CheckedProcess -FilePath $msbuildPath `
        -Arguments $unitTestProjectArguments `
        -Description 'Unit-test project'

    if ($Configuration -eq 'Release') {
        $releaseBuildLog = Join-Path -Path $resultsPath `
            -ChildPath 'release-artifact-build.log'
        $releaseBuildArguments = @(
            $releaseProject,
            '/t:Build',
            '/m',
            '/verbosity:minimal',
            '/p:WindowsTargetPlatformVersion=10.0',
            '/p:PlatformToolset=v143',
            "/p:Configuration=$Configuration",
            "/p:Platform=$Platform",
            "/p:SolutionDir=$repoRoot\",
            '/fl',
            "/flp:logfile=$releaseBuildLog;verbosity=normal"
        )
        Invoke-CheckedProcess -FilePath $msbuildPath `
            -Arguments $releaseBuildArguments `
            -Description 'Release artifact solution target'
    }

    if (-not (Test-Path -LiteralPath $unitTestExe -PathType Leaf)) {
        throw "Unit-test executable was not produced: $unitTestExe"
    }

    Invoke-UnitTestPhase `
        -Name 'libass-regression' `
        -Filter 'LibassBitmapTest.*:LibassRegressionTest.*' `
        -Repeat 50
    Invoke-UnitTestPhase `
        -Name 'remaining-unit-tests' `
        -Filter '*-OverallTest.*:LibassBitmapTest.*:LibassRegressionTest.*'
} finally {
    Pop-Location
}
