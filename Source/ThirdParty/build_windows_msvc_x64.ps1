# Build Umka with MSVC x64 (Visual Studio 2026)
# Copy this script to the umka-lang root folder and run it from any shell.
# Usage: powershell -ExecutionPolicy Bypass -File build_windows_msvc_x64.ps1

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# === Detect Visual Studio 2026 installation ===
$vsRoot = $null
foreach ($edition in @('BuildTools', 'Community', 'Professional', 'Enterprise')) {
    $candidate = "C:\Program Files (x86)\Microsoft Visual Studio\18\$edition"
    if (Test-Path "$candidate\Common7\Tools\VsDevCmd.bat") {
        $vsRoot = $candidate
        break
    }
}

if (-not $vsRoot) {
    Write-Error "ERROR: Could not find Visual Studio 2026. Install it with C++ build tools."
    exit 1
}

# === Find versioned MSVC bin directory ===
$msvcBase = Join-Path $vsRoot "VC\Tools\MSVC"
if (-not (Test-Path $msvcBase)) {
    Write-Error "ERROR: MSVC tools directory not found: $msvcBase"
    exit 1
}

$msvcBin = @(Get-ChildItem -LiteralPath $msvcBase -Directory) |
    Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName "bin\HostX64\x64" } |
    Where-Object { Test-Path (Join-Path $_ "cl.exe") } |
    Select-Object -First 1

if (-not $msvcBin) {
    Write-Error "ERROR: No MSVC version with cl.exe found under $msvcBase"
    exit 1
}

# === Initialize SDK environment via VsDevCmd.bat, then inject MSVC bin into PATH ===
Write-Host "Initializing MSVC x64 environment..."

# Capture env vars set by VsDevCmd.bat
$vsDevCmd = "$vsRoot\Common7\Tools\VsDevCmd.bat"
$envDump = cmd /c "`"$vsDevCmd`" -arch=x64 >nul 2>&1 && set" 2>$null
foreach ($line in $envDump) {
    if ($line -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}

# Prepend versioned MSVC compiler bin (VsDevCmd.bat doesn't add it for BuildTools)
$env:PATH = "$msvcBin;$env:PATH"

# VsDevCmd.bat omits MSVC CRT headers/libs for BuildTools — add them explicitly
# $msvcBin is  ...MSVC\<ver>\bin\HostX64\x64  →  go up 3 levels to get <ver> root
$msvcVersionRoot = Split-Path (Split-Path (Split-Path $msvcBin))
$msvcInclude = Join-Path $msvcVersionRoot "include"
$msvcLib     = Join-Path $msvcVersionRoot "lib\x64"
$env:INCLUDE = "$msvcInclude;$env:INCLUDE"
$env:LIB     = "$msvcLib;$env:LIB"

# Verify cl.exe is reachable
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Write-Error "ERROR: cl.exe not found after environment setup."
    exit 1
}

# === Switch to script's own directory (umka-lang root) ===
Set-Location -Path $PSScriptRoot

Write-Host "Building Umka (x64)..."
Set-Location src

# === Compile DLL and static library ===
$sources = @(
    'umka_api.c', 'umka_common.c', 'umka_compiler.c', 'umka_const.c', 'umka_decl.c',
    'umka_expr.c', 'umka_gen.c', 'umka_ident.c', 'umka_lexer.c', 'umka_runtime.c',
    'umka_stmt.c', 'umka_types.c', 'umka_vm.c'
)

& cl /nologo /O2 /MT /LD /MP /DWIN64 /DUMKA_BUILD /DUMKA_EXT_LIBS `
    /Fe:libumka.dll @sources `
    /link /MACHINE:X64 /IMPLIB:libumka.lib
if ($LASTEXITCODE -ne 0) { Write-Error "ERROR: cl failed (DLL)"; exit 1 }

& lib /nologo /MACHINE:X64 /OUT:libumka_static.lib (Get-Item *.obj)
if ($LASTEXITCODE -ne 0) { Write-Error "ERROR: lib failed"; exit 1 }

# === Compile CLI executable ===
& cl /nologo /O2 /MT /DWIN64 /Fe:umka.exe umka.c libumka.lib /link /MACHINE:X64
if ($LASTEXITCODE -ne 0) { Write-Error "ERROR: cl failed (CLI)"; exit 1 }

# === Cleanup ===
Remove-Item *.obj -ErrorAction SilentlyContinue
Set-Location ..

# === Prepare output directory ===
$outDir = "umka_windows_msvc"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Move-Item -Force src\libumka* $outDir
Move-Item -Force src\umka.exe $outDir
Copy-Item src\umka_api.h $outDir
Copy-Item LICENSE $outDir

# === Copy examples and documentation ===
foreach ($dir in @('examples\3dcam', 'examples\fractal', 'examples\lisp', 'examples\raytracer', 'doc', 'editors')) {
    New-Item -ItemType Directory -Force -Path "$outDir\$dir" | Out-Null
    Copy-Item "$dir\*.*" "$outDir\$dir" -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "=== Build completed successfully! ==="
