# build.ps1 — build, deploy, and prepare Gen VST for end-to-end testing (Windows).
#
# Configures and builds the plugin via CMake, installs the factory patches into
# the per-user data directory, copies the VST3 into a plug-in folder a DAW will
# scan, and optionally launches the Standalone. Interim developer convenience —
# the shippable installer is a later task.
#
#   .\build.ps1 [--release] [--system] [--run] [--clean] [--help]
#
# If running .ps1 scripts is blocked, invoke as:
#   pwsh -ExecutionPolicy Bypass -File .\build.ps1

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# ---- output helpers ---------------------------------------------------------
function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Info($msg) { Write-Host "    $msg" }
function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

function Show-Usage {
    Write-Host @'
build.ps1 — build, deploy and prepare Gen VST for end-to-end testing.

  .\build.ps1 [--release] [--system] [--run] [--clean] [--help]

  --release   Build/deploy the Release configuration (default: Debug).
  --system    Deploy to %CommonProgramFiles%\VST3 (needs elevation).
              Default: %LOCALAPPDATA%\Programs\Common\VST3 (no elevation).
  --run       Launch the Standalone after a successful deploy.
  --clean     Delete the build directory first (full reconfigure).
  --help      Show this help and exit.
'@
}

# ---- parse arguments --------------------------------------------------------
$optRelease = $false
$optSystem  = $false
$optRun     = $false
$optClean   = $false

foreach ($arg in $args) {
    switch -Regex ($arg) {
        '^(--release|-Release|-r)$'   { $optRelease = $true }
        '^(--system|-System|-s)$'     { $optSystem  = $true }
        '^(--run|-Run)$'              { $optRun     = $true }
        '^(--clean|-Clean)$'          { $optClean   = $true }
        '^(--help|-Help|-h|-\?|/\?)$' { Show-Usage; exit 0 }
        default { Show-Usage; Fail "Unknown option: $arg" }
    }
}

$config   = if ($optRelease) { 'Release' } else { 'Debug' }
$preset   = if ($optRelease) { 'windows-release' } else { 'windows-debug' }
$buildDir = Join-Path $PSScriptRoot "build\$preset"

# ---- toolchain resolution ---------------------------------------------------
function Resolve-CMake {
    # 1. cmake on PATH.
    $onPath = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    # 2. cmake bundled with Visual Studio (located via vswhere).
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -property installationPath 2>$null
        if ($vsPath) {
            $candidate = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $candidate) { return $candidate }
        }
    }

    # 3. Standalone CMake install.
    $standalone = Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'
    if (Test-Path $standalone) { return $standalone }

    Fail @'
cmake not found. Looked on PATH, in the Visual Studio bundle, and in
"C:\Program Files\CMake". Install CMake (https://cmake.org/download/) or add the
"C++ CMake tools for Windows" component to your Visual Studio install.
'@
}

$cmake = Resolve-CMake

foreach ($tool in @('node', 'npm')) {
    if (-not (Get-Command $tool -CommandType Application -ErrorAction SilentlyContinue)) {
        Fail "$tool not found on PATH. Node.js is required to build the web UI — install it from https://nodejs.org/."
    }
}

# ---- helpers ----------------------------------------------------------------
function Invoke-CMake {
    & $cmake @args
    if ($LASTEXITCODE -ne 0) { Fail "cmake $($args -join ' ') failed (exit $LASTEXITCODE)." }
}

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal $id).IsInRole(
        [Security.Principal.WindowsBuiltinRole]::Administrator)
}

function Quote($s) { "'" + ($s -replace "'", "''") + "'" }

# Replace the bundle at $dest with a fresh recursive copy of $src.
function Copy-Bundle($src, $dest) {
    $parent = Split-Path -Parent $dest
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    Copy-Item -Recurse -Force -Path $src -Destination $dest
}

# ---- banner -----------------------------------------------------------------
Write-Host ""
Write-Step "Gen VST — build & deploy"
Write-Info "config : $config"
Write-Info "preset : $preset"
Write-Info "cmake  : $cmake"
Write-Info "deploy : $(if ($optSystem) { 'system VST3 folder (elevated)' } else { 'per-user VST3 folder' })"
Write-Host ""

# ---- clean ------------------------------------------------------------------
if ($optClean -and (Test-Path $buildDir)) {
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
$patchDir    = Join-Path $env:LOCALAPPDATA 'GenVst\patches'
$patchSrcDir = Join-Path $PSScriptRoot 'extern\patches'
$patchExts   = '*.tfi','*.vgi','*.dmp','*.y12','*.opm','*.psg'
$patchFiles  = @(Get-ChildItem -Path $patchSrcDir -File -Recurse -Include $patchExts)
if ($patchFiles.Count -eq 0) { Fail "No factory patches found in $patchSrcDir" }
if (Test-Path $patchDir) { Remove-Item -Recurse -Force $patchDir }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $patchDir) | Out-Null
Copy-Item -Recurse -Force -Path $patchSrcDir -Destination $patchDir
Write-Info "$($patchFiles.Count) patch file(s) -> $patchDir"

# ---- deploy the VST3 --------------------------------------------------------
$artefacts = Join-Path $buildDir "src\GenVst_artefacts\$config"
$vst3Src   = Join-Path $artefacts 'VST3\Gen VST.vst3'
if (-not (Test-Path $vst3Src)) { Fail "Built VST3 not found at: $vst3Src" }

$vst3Root = if ($optSystem) {
    Join-Path $env:CommonProgramFiles 'VST3'
} else {
    Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
}
$vst3Dest = Join-Path $vst3Root 'Gen VST.vst3'

Write-Step "Deploying VST3 -> $vst3Dest"
if ($optSystem -and -not (Test-Admin)) {
    # Build stays unprivileged; only the copy into the system folder elevates.
    Write-Info "System folder requires elevation — a UAC prompt will appear for the copy."
    $elevated =
        "`$ErrorActionPreference='Stop'; " +
        "New-Item -ItemType Directory -Force -Path $(Quote $vst3Root) | Out-Null; " +
        "if (Test-Path $(Quote $vst3Dest)) { Remove-Item -Recurse -Force $(Quote $vst3Dest) }; " +
        "Copy-Item -Recurse -Force -Path $(Quote $vst3Src) -Destination $(Quote $vst3Dest)"
    $psHost = (Get-Process -Id $PID).Path
    try {
        $proc = Start-Process -FilePath $psHost -Verb RunAs -Wait -PassThru `
                    -ArgumentList '-NoProfile', '-NonInteractive', '-Command', $elevated
    } catch {
        Fail "Elevation was cancelled. The plugin built successfully but was not copied to the system folder.`nRe-run from an elevated terminal, or omit --system to deploy to the per-user folder."
    }
    if ($proc.ExitCode -ne 0) { Fail "Elevated copy failed (exit $($proc.ExitCode))." }
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
Write-Info "  * Run the Standalone above (or re-run with --run) — the pixel-art UI should appear."
Write-Info "  * In your DAW, rescan plug-ins and load 'Gen VST' (Instrument/Synth); play MIDI."
Write-Host ""

# ---- run --------------------------------------------------------------------
if ($optRun) {
    if (Test-Path $standalone) {
        Write-Step "Launching Standalone"
        Start-Process -FilePath $standalone
    } else {
        Write-Info "Standalone not found at $standalone — skipping --run."
    }
}
