<#
.SYNOPSIS
    Build, deploy, and manage a developer install of Gen VST on Windows.

.DESCRIPTION
    Configures and builds the plugin via CMake, installs factory patches into
    the per-user data directory, and copies the VST3 bundle into a folder a DAW
    will scan. Optionally launches the Standalone for a quick smoke-test.

    Use -Uninstall to remove a previously deployed developer build (VST3 bundle
    and factory patches) without triggering a build.

    If running .ps1 scripts is blocked, invoke as:
        pwsh -ExecutionPolicy Bypass -File .\build.ps1

.PARAMETER Release
    Build and deploy the Release configuration. Default: Debug.

.PARAMETER System
    Deploy to %CommonProgramFiles%\VST3 (requires elevation via UAC).
    Default: %LOCALAPPDATA%\Programs\Common\VST3 (no elevation required).

.PARAMETER Run
    Launch the Standalone executable after a successful deploy.

.PARAMETER Clean
    Delete the CMake build directory before configuring (full reconfigure).
    When combined with -Uninstall, also removes the build directory.

.PARAMETER Uninstall
    Remove the developer-deployed VST3 bundle and factory patches from the
    installed locations without building. Combine with -System to target the
    system VST3 folder. Combine with -Clean to also wipe the build directory.

.EXAMPLE
    .\build.ps1
    Debug build, deploy to per-user VST3 folder.

.EXAMPLE
    .\build.ps1 -Release -Run
    Release build, deploy, then launch the Standalone.

.EXAMPLE
    .\build.ps1 -Release -System
    Release build, deploy to the system VST3 folder (UAC prompt).

.EXAMPLE
    .\build.ps1 -Clean
    Wipe the build directory and do a full reconfigure + Debug build.

.EXAMPLE
    .\build.ps1 -Uninstall
    Remove the per-user developer VST3 install and factory patches.

.EXAMPLE
    .\build.ps1 -Uninstall -System
    Remove the system-folder developer VST3 install and factory patches (UAC prompt).

.EXAMPLE
    .\build.ps1 -Uninstall -Clean
    Remove installed assets and wipe the build directory.
#>
[CmdletBinding()]
param(
    [switch] $Release,
    [switch] $System,
    [switch] $Run,
    [switch] $Clean,
    [switch] $Uninstall
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# ---- output helpers ---------------------------------------------------------
function Write-Step([string] $Message) { Write-Host "==> $Message" -ForegroundColor Cyan }
function Write-Info([string] $Message) { Write-Host "    $Message" }
function Exit-Fatal([string] $Message) { Write-Host "ERROR: $Message" -ForegroundColor Red; exit 1 }

# ---- shared paths -----------------------------------------------------------
$config   = if ($Release) { 'Release' } else { 'Debug' }
$preset   = if ($Release) { 'windows-release' } else { 'windows-debug' }
$buildDir = Join-Path $PSScriptRoot "build\$preset"
$patchDir = Join-Path $env:LOCALAPPDATA 'GenVst\patches'
$vst3Root = if ($System) { Join-Path $env:CommonProgramFiles 'VST3' } `
            else          { Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3' }
$vst3Dest = Join-Path $vst3Root 'Gen VST.vst3'

# ---- helpers ----------------------------------------------------------------
function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal $id).IsInRole(
        [Security.Principal.WindowsBuiltinRole]::Administrator)
}

function ConvertTo-SingleQuoted([string] $Value) {
    "'" + ($Value -replace "'", "''") + "'"
}

function Copy-Bundle([string] $Source, [string] $Destination) {
    $parent = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    if (Test-Path $Destination) { Remove-Item -Recurse -Force $Destination }
    Copy-Item -Recurse -Force -Path $Source -Destination $Destination
}

# Runs $Script in an elevated PowerShell process.
# Throws if UAC is cancelled; calls Exit-Fatal if the elevated script exits non-zero.
function Invoke-Elevated([string] $Script, [string] $FailMessage = 'Elevated operation failed') {
    $psHost = (Get-Process -Id $PID).Path
    $proc   = Start-Process -FilePath $psHost -Verb RunAs -Wait -PassThru `
                  -ArgumentList '-NoProfile', '-NonInteractive', '-Command', $Script
    if ($proc.ExitCode -ne 0) { Exit-Fatal "$FailMessage (exit $($proc.ExitCode))." }
}

# ---- uninstall --------------------------------------------------------------
if ($Uninstall) {
    Write-Host ""
    Write-Step "Gen VST — uninstall developer build"
    Write-Info "VST3   : $vst3Dest"
    Write-Info "Patches: $patchDir"
    if ($Clean) { Write-Info "Build  : $buildDir (will be removed)" }
    Write-Host ""

    if ($System -and -not (Test-Admin)) {
        Write-Info "System folder requires elevation — a UAC prompt will appear."
        $elevated =
            "`$ErrorActionPreference='Stop'; " +
            "if (Test-Path $(ConvertTo-SingleQuoted $vst3Dest)) " +
            "{ Remove-Item -Recurse -Force $(ConvertTo-SingleQuoted $vst3Dest) }"
        try { Invoke-Elevated $elevated 'Elevated removal failed' }
        catch { Exit-Fatal "Elevation was cancelled. Nothing was removed." }
        Write-Info "Removed: $vst3Dest"
    } elseif (Test-Path $vst3Dest) {
        Remove-Item -Recurse -Force $vst3Dest
        Write-Info "Removed: $vst3Dest"
    } else {
        Write-Info "Not present: $vst3Dest"
    }

    if (Test-Path $patchDir) {
        Remove-Item -Recurse -Force $patchDir
        Write-Info "Removed: $patchDir"
    } else {
        Write-Info "Not present: $patchDir"
    }

    if ($Clean -and (Test-Path $buildDir)) {
        Write-Step "Cleaning $buildDir"
        Remove-Item -Recurse -Force $buildDir
    }

    Write-Host ""
    Write-Step "Done."
    Write-Host ""
    exit 0
}

# ---- toolchain resolution ---------------------------------------------------
function Resolve-CMake {
    $onPath = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -property installationPath 2>$null
        if ($vsPath) {
            $candidate = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $candidate) { return $candidate }
        }
    }

    $standalone = Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'
    if (Test-Path $standalone) { return $standalone }

    Exit-Fatal @'
cmake not found. Looked on PATH, in the Visual Studio bundle, and in
"C:\Program Files\CMake". Install CMake (https://cmake.org/download/) or add the
"C++ CMake tools for Windows" component to your Visual Studio install.
'@
}

$cmake = Resolve-CMake

foreach ($tool in @('node', 'npm')) {
    if (-not (Get-Command $tool -CommandType Application -ErrorAction SilentlyContinue)) {
        Exit-Fatal "$tool not found on PATH. Node.js is required to build the web UI — install it from https://nodejs.org/."
    }
}

function Invoke-CMake {
    & $cmake @args
    if ($LASTEXITCODE -ne 0) { Exit-Fatal "cmake $($args -join ' ') failed (exit $LASTEXITCODE)." }
}

# ---- banner -----------------------------------------------------------------
Write-Host ""
Write-Step "Gen VST — build & deploy"
Write-Info "config : $config"
Write-Info "preset : $preset"
Write-Info "cmake  : $cmake"
Write-Info "deploy : $(if ($System) { 'system VST3 folder (elevated)' } else { 'per-user VST3 folder' })"
Write-Host ""

# ---- clean ------------------------------------------------------------------
if ($Clean -and (Test-Path $buildDir)) {
    Write-Step "Cleaning $buildDir"
    Remove-Item -Recurse -Force $buildDir
}

# ---- configure --------------------------------------------------------------
# GENVST_DEV_SERVER=OFF / COPY_PLUGIN_AFTER_BUILD=OFF are set explicitly so this
# always produces the self-contained embedded-bundle plugin and never triggers
# JUCE's own post-build copy into the system folder.
Write-Step "Configuring (preset: $preset)"
Invoke-CMake --preset $preset -DGENVST_DEV_SERVER=OFF -DCOPY_PLUGIN_AFTER_BUILD=OFF

# ---- build ------------------------------------------------------------------
Write-Step "Building $config (this also builds the Vite web UI bundle)"
Invoke-CMake --build $buildDir --config $config

# ---- install factory patches ------------------------------------------------
# Mirror the full extern/patches/ tree (fm/<category>/*, sq/*) into the per-
# user data dir so the Standalone reads the same layout the VST3 bundle has.
# `cmake --install` is intentionally NOT used: it would also run JUCE's own
# framework install rules. The gitignored extra/ developer set never reaches
# this glob — it lives in a sibling directory (ADR-0004).
Write-Step "Installing factory patches"
$patchSrcDir = Join-Path $PSScriptRoot 'extern\patches'
$patchExts   = '*.tfi','*.vgi','*.dmp','*.y12','*.opm','*.psg'
$patchFiles  = @(Get-ChildItem -Path $patchSrcDir -File -Recurse -Include $patchExts)
if ($patchFiles.Count -eq 0) { Exit-Fatal "No factory patches found in $patchSrcDir" }
if (Test-Path $patchDir) { Remove-Item -Recurse -Force $patchDir }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $patchDir) | Out-Null
Copy-Item -Recurse -Force -Path $patchSrcDir -Destination $patchDir
Write-Info "$($patchFiles.Count) patch file(s) -> $patchDir"

# ---- deploy the VST3 --------------------------------------------------------
$artefacts = Join-Path $buildDir "src\GenVst_artefacts\$config"
$vst3Src   = Join-Path $artefacts 'VST3\Gen VST.vst3'
if (-not (Test-Path $vst3Src)) { Exit-Fatal "Built VST3 not found at: $vst3Src" }

Write-Step "Deploying VST3 -> $vst3Dest"
if ($System -and -not (Test-Admin)) {
    Write-Info "System folder requires elevation — a UAC prompt will appear for the copy."
    $elevated =
        "`$ErrorActionPreference='Stop'; " +
        "New-Item -ItemType Directory -Force -Path $(ConvertTo-SingleQuoted $vst3Root) | Out-Null; " +
        "if (Test-Path $(ConvertTo-SingleQuoted $vst3Dest)) { Remove-Item -Recurse -Force $(ConvertTo-SingleQuoted $vst3Dest) }; " +
        "Copy-Item -Recurse -Force -Path $(ConvertTo-SingleQuoted $vst3Src) -Destination $(ConvertTo-SingleQuoted $vst3Dest)"
    try { Invoke-Elevated $elevated 'Elevated copy failed' }
    catch { Exit-Fatal "Elevation was cancelled. The plugin built successfully but was not copied to the system folder.`nRe-run from an elevated terminal, or omit -System to deploy to the per-user folder." }
} else {
    Copy-Bundle $vst3Src $vst3Dest
}

# ---- summary ----------------------------------------------------------------
$standalone = Join-Path $artefacts 'Standalone\Gen VST.exe'

Write-Host ""
Write-Step "Done."
Write-Info "VST3 deployed  : $vst3Dest"
Write-Info "Standalone     : $standalone"
Write-Info "Factory patches: $patchDir"
Write-Host ""
Write-Info "Test end-to-end:"
Write-Info "  * Run the Standalone above (or re-run with -Run) — the pixel-art UI should appear."
Write-Info "  * In your DAW, rescan plug-ins and load 'Gen VST' (Instrument/Synth); play MIDI."
Write-Host ""

# ---- run --------------------------------------------------------------------
if ($Run) {
    if (Test-Path $standalone) {
        Write-Step "Launching Standalone"
        Start-Process -FilePath $standalone
    } else {
        Write-Info "Standalone not found at $standalone — skipping -Run."
    }
}
