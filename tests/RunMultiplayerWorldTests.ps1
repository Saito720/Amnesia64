param([switch]$ProtocolOnly)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Install Visual Studio Desktop development with C++ first.' }
$compilerRoot = Get-ChildItem -LiteralPath (Join-Path $installation 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdkVersion = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$compiler = Join-Path $compilerRoot.FullName 'bin/Hostx64/x64/cl.exe'
$output = Join-Path $repo 'x64/Debug'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$common = @('/nologo', '/std:c++17', '/EHsc', '/MDd', '/D_DEBUG', "/I$($compilerRoot.FullName)/include", "/I$($sdkVersion.FullName)/ucrt")
$link = @('/link', "/LIBPATH:$($compilerRoot.FullName)/lib/x64", "/LIBPATH:$sdkRoot/Lib/$($sdkVersion.Name)/ucrt/x64", "/LIBPATH:$sdkRoot/Lib/$($sdkVersion.Name)/um/x64")
$protocol = Join-Path $output 'multiplayer_world_protocol_tests.exe'
& $compiler @common /W4 (Join-Path $PSScriptRoot 'multiplayer_world_protocol_tests.cpp') "/Fe$protocol" "/Fo$output/multiplayer_world_protocol_tests.obj" @link
if ($LASTEXITCODE) { throw 'Protocol test compilation failed.' }
& $protocol
if ($LASTEXITCODE) { throw 'Protocol tests failed.' }
if ($ProtocolOnly) { return }
if (-not (Test-Path -LiteralPath (Join-Path $output 'HPL2.lib'))) { throw 'Build Amnesia.sln Debug x64 before the Newton smoke test.' }
$engine = @('/W3', '/Gy', '/DUSE_SDL2', '/D_NEWTON_USE_LIB', '/DMEMORY_MANAGER_ACTIVE', '/DGLEW_STATIC', '/DIL_STATIC_LIB', '/DAL_LIBTYPE_STATIC', '/DHAVE_LIBC',
    "/I$repo/HPL2/core/include", "/I$repo/HPL2/dependencies/include", "/I$repo/HPL2/dependencies/include/SDL2", "/I$($sdkVersion.FullName)/um", "/I$($sdkVersion.FullName)/shared")
$libraries = @('HPL2.lib', 'Newton.lib', 'SDL2.lib', 'OpenAL32.lib', 'freealut.lib', 'GLEW.lib', 'DevIL.lib', 'AngelScript.lib', 'zlib.lib', 'jpeg.lib', 'png.lib', 'ogg.lib', 'vorbis.lib', 'theora.lib',
    'dbghelp.lib', 'winmm.lib', 'setupapi.lib', 'Imm32.lib', 'Version.lib', 'Avrt.lib', 'opengl32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib', 'ole32.lib', 'oleaut32.lib', 'ws2_32.lib', 'advapi32.lib')
$smoke = Join-Path $output 'multiplayer_world_newton_smoke.exe'
& $compiler @common @engine (Join-Path $PSScriptRoot 'multiplayer_world_newton_smoke.cpp') "/Fe$smoke" "/Fo$output/multiplayer_world_newton_smoke.obj" @link /OPT:REF "/LIBPATH:$output" @libraries
if ($LASTEXITCODE) { throw 'Newton smoke test compilation failed.' }
& $smoke
if ($LASTEXITCODE) { throw 'Newton smoke test failed.' }
