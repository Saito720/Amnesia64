param([Parameter(Mandatory=$true)][string]$AssetsDirectory)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$assets = (Resolve-Path -LiteralPath $AssetsDirectory).Path
if (!(Test-Path -LiteralPath (Join-Path $assets 'core/shaders/deferred_base_vtx.glsl'))) { throw 'Pass the installed game directory containing core/shaders.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Visual Studio C++ tools are required.' }
$version = (Get-Content (Join-Path $installation 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt')).Trim()
$toolset = Join-Path $installation "VC/Tools/MSVC/$version"
$sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdkVersion = (Get-ChildItem (Join-Path $sdk 'Include') -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'um/Windows.h') } | Sort-Object Name -Descending | Select-Object -First 1).Name
$outDirectory = Join-Path $workspace 'bld/uncapped-fps-render-smoke'
New-Item -ItemType Directory -Force -Path $outDirectory | Out-Null
$previousInclude = $env:INCLUDE
$previousLib = $env:LIB
try {
    $env:INCLUDE = "$toolset/include;$sdk/Include/$sdkVersion/ucrt;$sdk/Include/$sdkVersion/shared;$sdk/Include/$sdkVersion/um;$sdk/Include/$sdkVersion/winrt"
    $env:LIB = "$toolset/lib/x64;$sdk/Lib/$sdkVersion/ucrt/x64;$sdk/Lib/$sdkVersion/um/x64"
    $libraries = @('HPL2','Newton','AngelScript','DevIL','freealut','GLEW','jpeg','ogg','OpenAL32','png','SDL2','theora','vorbis','zlib') | ForEach-Object { Join-Path $workspace "x64/Release/$_.lib" }
    $systemLibraries = @('dbghelp.lib','winmm.lib','setupapi.lib','Imm32.lib','Version.lib','Avrt.lib','user32.lib','gdi32.lib','shell32.lib','ole32.lib','oleaut32.lib','advapi32.lib','uuid.lib')
    $options = @('/nologo','/EHsc','/MD','/O2','/DUSE_SDL2','/DUSE_GAMEPAD','/DGLEW_STATIC','/D_NEWTON_USE_LIB','/DIL_STATIC_LIB','/DHAVE_LIBC','/DAL_LIBTYPE_STATIC',"/I$workspace/HPL2/core/include","/I$workspace/HPL2/dependencies/include",(Join-Path $PSScriptRoot 'RenderSmoke.cpp'),"/Fo$outDirectory/RenderSmoke.obj","/Fe$outDirectory/RenderSmoke.exe")
    & "$toolset/bin/Hostx64/x64/cl.exe" @options @libraries /link /LTCG /SUBSYSTEM:CONSOLE /INCREMENTAL:NO @systemLibraries
    if ($LASTEXITCODE -ne 0) { throw 'Render smoke compilation failed. Build Release x64 first.' }
    $process = Start-Process -FilePath (Join-Path $outDirectory 'RenderSmoke.exe') -ArgumentList ('"'+$outDirectory+'"') -WorkingDirectory $assets -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $outDirectory 'stdout.txt') -RedirectStandardError (Join-Path $outDirectory 'stderr.txt')
    if (!$process.WaitForExit(45000)) { $process.Kill(); throw 'Render smoke did not finish within 45 seconds; see bld/uncapped-fps-render-smoke logs.' }
    Get-Content (Join-Path $outDirectory 'stdout.txt')
    Get-Content (Join-Path $outDirectory 'stderr.txt')
    if ($process.ExitCode -ne 0) { throw "Render smoke failed with exit code $($process.ExitCode)." }
} finally {
    $env:INCLUDE = $previousInclude
    $env:LIB = $previousLib
}
