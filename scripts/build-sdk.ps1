<#
.SYNOPSIS
Build and validate an isolated Windows development SDK without activating it.
.DESCRIPTION
Run in a Visual Studio developer environment against an already configured,
single-configuration Release build (LLVM, tests, experimental tools ON and
CMAKE_INSTALL_BINDIR=.). Every invocation creates a unique candidate directory.
Failed candidates and their evidence are retained for diagnosis. This script
never invokes publish-sdk.py, changes an active SDK, or declares an RC/release.
.EXAMPLE
pwsh -File scripts/build-sdk.ps1 -BuildDirectory C:\nil\build\cmake -AllowDirtySource
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BuildDirectory,
    [string] $CandidateParent,
    [string] $Python = 'python',
    [ValidateRange(1, 64)]
    [int] $Parallel = 4,
    [switch] $AllowDirtySource
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-Checked {
    param([string] $Command, [string[]] $CommandArguments)
    & $Command @CommandArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit ${LASTEXITCODE}: $Command"
    }
}

function Resolve-RequiredApplication {
    param([string] $Name)
    # Tool names use PATH; explicit paths use literal lookup, including [] in
    # workspace/tool directories. Do not invoke aliases, functions, or scripts.
    if ($Name.Contains('\') -or $Name.Contains('/')) {
        if ((Test-Path -LiteralPath $Name -PathType Leaf) -and
            [IO.Path]::GetExtension($Name) -ieq '.exe') {
            return (Get-Item -LiteralPath $Name).FullName
        }
    } else {
        $application = Get-Command -Name $Name -CommandType Application -ErrorAction SilentlyContinue |
            Where-Object { [IO.Path]::GetExtension($_.Source) -ieq '.exe' } |
            Select-Object -First 1
        if ($application) {
            return $application.Source
        }
    }
    throw "Required application '$Name' is unavailable. Add it to the inherited developer environment before building the SDK. No candidate was created."
}

if ($env:OS -ne 'Windows_NT') {
    throw 'This development SDK workflow requires Windows.'
}
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).ProviderPath
if ([string]::IsNullOrWhiteSpace($CandidateParent)) {
    # Windows PowerShell 5.1 may bind parameter defaults before PSScriptRoot
    # is populated. Resolve this script-relative default in the script body.
    $CandidateParent = Join-Path $repoRoot 'build\windows-candidates'
}
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).ProviderPath
$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    throw 'BuildDirectory must contain an existing configured CMake build.'
}
$cache = Get-Content -LiteralPath $cachePath -Raw -Encoding UTF8
foreach ($setting in @(
    @('CMAKE_BUILD_TYPE', 'Release'),
    @('NIL_ENABLE_LLVM', 'ON'),
    @('NIL_BUILD_EXPERIMENTAL_TOOLS', 'ON'),
    @('BUILD_TESTING', 'ON'),
    @('CMAKE_INSTALL_BINDIR', '.')
)) {
    $expression = '(?m)^' + [regex]::Escape($setting[0]) + ':[^=]+=' +
                  [regex]::Escape($setting[1]) + '\r?$'
    if ($cache -notmatch $expression) {
        throw "Configured build requires $($setting[0])=$($setting[1])."
    }
}
if ($cache -match '(?m)^CMAKE_CONFIGURATION_TYPES:[^=]+=.+$') {
    throw 'Use a single-configuration build (for example Ninja), not Visual Studio multi-config.'
}
$sourceMatch = [regex]::Match($cache, '(?m)^CMAKE_HOME_DIRECTORY:[^=]+=(.+?)\r?$')
if (-not $sourceMatch.Success -or
    (Resolve-Path -LiteralPath $sourceMatch.Groups[1].Value).ProviderPath -ne $repoRoot) {
    throw 'Configured build belongs to a different source checkout.'
}

# A task launched by desktop VS Code does not inherit vcvars automatically.
# Fail before creating a candidate rather than importing a guessed VS install,
# running a generated cmd string, or changing the user's environment/settings.
if ([string]::IsNullOrWhiteSpace($env:VSCMD_VER) -or
    $env:VSCMD_ARG_TGT_ARCH -ine 'x64' -or
    [string]::IsNullOrWhiteSpace($env:INCLUDE) -or
    [string]::IsNullOrWhiteSpace($env:LIB)) {
    throw ('An x64 Visual Studio developer environment is required. Close VS Code and launch it from an x64 Native Tools Command Prompt or Developer PowerShell configured for x64. ' +
           'CMake, CTest, Git and Python must also be available there. No candidate was created.')
}
$null = Resolve-RequiredApplication 'cl.exe'
$null = Resolve-RequiredApplication 'link.exe'
$cmake = Resolve-RequiredApplication 'cmake.exe'
$ctest = Resolve-RequiredApplication 'ctest.exe'
$git = Resolve-RequiredApplication 'git.exe'
$Python = Resolve-RequiredApplication $Python

$revision = (& $git -C $repoRoot rev-parse --verify HEAD)
if ($LASTEXITCODE -ne 0 -or $revision -notmatch '^[0-9a-f]{40,64}$') {
    throw 'Could not determine the source revision.'
}
$sourceStatus = (& $git -C $repoRoot status --porcelain=v1 --untracked-files=all)
if ($LASTEXITCODE -ne 0) {
    throw 'Could not determine the source worktree status.'
}
if ($sourceStatus -and -not $AllowDirtySource) {
    throw 'Source is dirty. Commit first or explicitly use -AllowDirtySource for development evidence.'
}

# New names only: no caller-supplied SDK output can replace a previous SDK.
New-Item -ItemType Directory -Force -Path $CandidateParent | Out-Null
$candidateParentPath = (Resolve-Path -LiteralPath $CandidateParent).ProviderPath
$candidate = Join-Path $candidateParentPath ('development-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $candidate | Out-Null
$sdk = Join-Path $candidate 'sdk'
$evidence = Join-Path $candidate 'build-evidence.json'
$ctestReport = Join-Path $candidate 'ctest.xml'
$consumerReport = Join-Path $candidate 'consumer-routes.json'
$transcriptStarted = $false

Write-Host "Windows development candidate: $candidate"
try {
    Start-Transcript -Path (Join-Path $candidate 'build.log') -NoClobber | Out-Null
    $transcriptStarted = $true
    Invoke-Checked $cmake @('--build', $buildRoot, '--parallel', [string]$Parallel)
    Invoke-Checked $Python @(
        (Join-Path $PSScriptRoot 'sdk_qualification.py'),
        '--repo-root', $repoRoot, '--build-dir', $buildRoot,
        '--revision', $revision, '--tests', 'ON', '--output', $evidence
    )
    Invoke-Checked $ctest @(
        '--test-dir', $buildRoot, '--output-on-failure', '--parallel', [string]$Parallel,
        '--output-junit', $ctestReport
    )
    Invoke-Checked $cmake @('--install', $buildRoot, '--prefix', $sdk)
    Invoke-Checked $Python @(
        (Join-Path $PSScriptRoot 'build-docs.py'), '--repo-root', $repoRoot,
        '--output', (Join-Path $sdk 'docs'), '--revision', $revision
    )
    $packageArguments = @(
        (Join-Path $PSScriptRoot 'package-sdk.py'), '--repo-root', $repoRoot,
        '--stage-dir', $sdk, '--revision', $revision, '--build-type', 'Release',
        '--llvm', 'ON', '--qualification', 'development',
        '--build-evidence', $evidence, '--ctest-report', $ctestReport
    )
    if ($AllowDirtySource) {
        $packageArguments += '--allow-dirty-source'
    }
    Invoke-Checked $Python $packageArguments
    Invoke-Checked $Python @(
        (Join-Path $PSScriptRoot 'validate-sdk.py'), $sdk, '--expected-revision', $revision
    )
    Invoke-Checked $Python @(
        (Join-Path $PSScriptRoot 'check-sdk-routes.py'), '--sdk', $sdk,
        '--expected-revision', $revision, '--output', $consumerReport
    )
} catch {
    Set-Content -LiteralPath (Join-Path $candidate 'FAILED.txt') -Encoding UTF8 -Value (
        'This candidate did not complete Windows development SDK validation.' +
        [Environment]::NewLine + [string]$_
    )
    Write-Warning "Candidate and evidence preserved for diagnosis: $candidate"
    throw
} finally {
    if ($transcriptStarted) {
        Stop-Transcript | Out-Null
    }
}
Write-Host "Validated Windows development SDK: $sdk"
Write-Host "Consumer evidence: $consumerReport"
Write-Host 'Not activated; not RC/release-qualified; no publication performed.'
