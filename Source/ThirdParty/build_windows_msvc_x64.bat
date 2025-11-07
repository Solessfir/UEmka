@echo off
setlocal enabledelayedexpansion

rem Copy this batch file to the umka-lang root folder and run it to build Umka with MSVC x64

rem === Detect Visual Studio Dev Environment ===
set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7"

if not exist "%VSROOT%\Tools\VsDevCmd.bat" (
    echo ERROR: Could not find VsDevCmd.bat in "%VSROOT%\Tools"
    echo Please verify Visual Studio Community 2022 is installed with C++ build tools.
    pause
    exit /b 1
)

echo Initializing MSVC x64 environment...
call "%VSROOT%\Tools\VsDevCmd.bat" -arch=x64

rem === Go to project root (this script's directory) ===
cd /d "%~dp0"

echo Building Umka (x64)...
cd src

rem === Compile DLL and static library ===
cl /nologo /O2 /MT /LD /MP /DWIN64 /DUMKA_BUILD /DUMKA_EXT_LIBS ^
    /Fe:libumka.dll ^
    umka_api.c umka_common.c umka_compiler.c umka_const.c umka_decl.c umka_expr.c ^
    umka_gen.c umka_ident.c umka_lexer.c umka_runtime.c umka_stmt.c umka_types.c umka_vm.c ^
    /link /MACHINE:X64 /IMPLIB:libumka.lib

lib /nologo /MACHINE:X64 /OUT:libumka_static.lib *.obj

rem === Compile CLI executable ===
cl /nologo /O2 /MT /DWIN64 /Fe:umka.exe umka.c libumka.lib /link /MACHINE:X64

rem === Cleanup ===
del *.obj
cd ..

rem === Prepare output directory ===
set "OUTDIR=umka_windows_msvc"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

move /y src\libumka* "%OUTDIR%"
move /y src\umka.exe "%OUTDIR%"
copy src\umka_api.h "%OUTDIR%"
copy LICENSE "%OUTDIR%"

rem === Copy examples and documentation ===
mkdir "%OUTDIR%\examples"
mkdir "%OUTDIR%\examples\3dcam"
mkdir "%OUTDIR%\examples\fractal"
mkdir "%OUTDIR%\examples\lisp"
mkdir "%OUTDIR%\examples\raytracer"
mkdir "%OUTDIR%\doc"
mkdir "%OUTDIR%\editors"

copy examples\3dcam\*.* "%OUTDIR%\examples\3dcam"
copy examples\fractal\*.* "%OUTDIR%\examples\fractal"
copy examples\lisp\*.* "%OUTDIR%\examples\lisp"
copy examples\raytracer\*.* "%OUTDIR%\examples\raytracer"
copy doc\*.* "%OUTDIR%\doc"
copy editors\*.* "%OUTDIR%\editors"

echo.
echo === Build completed successfully! ===
endlocal
pause
