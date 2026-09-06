param([switch]$SkipBuild, [string]$Test = '*')
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Visual Studio C++ tools are required.' }
$msbuild = Join-Path $installation 'MSBuild/Current/Bin/amd64/MSBuild.exe'
if (!$SkipBuild) {
    & $msbuild (Join-Path $workspace 'Amnesia.sln') /t:Lux /p:Configuration=Release /p:Platform=x64 /m /verbosity:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw 'Game build failed.' }
}
$version = (Get-Content (Join-Path $installation 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt')).Trim()
$toolset = Join-Path $installation "VC/Tools/MSVC/$version"
$sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdkVersion = (Get-ChildItem (Join-Path $sdk 'Include') -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'um/Windows.h') } | Sort-Object Name -Descending | Select-Object -First 1).Name
$outDirectory = Join-Path $workspace 'bld/uncapped-fps-tests'
New-Item -ItemType Directory -Force -Path $outDirectory | Out-Null
$previousInclude = $env:INCLUDE
$previousLib = $env:LIB
try {
    $env:INCLUDE = "$toolset/include;$sdk/Include/$sdkVersion/ucrt;$sdk/Include/$sdkVersion/shared;$sdk/Include/$sdkVersion/um;$sdk/Include/$sdkVersion/winrt"
    $env:LIB = "$toolset/lib/x64;$sdk/Lib/$sdkVersion/ucrt/x64;$sdk/Lib/$sdkVersion/um/x64"
    $libraries = @('HPL2','Newton','AngelScript','DevIL','freealut','GLEW','jpeg','ogg','OpenAL32','png','SDL2','theora','vorbis','zlib') | ForEach-Object { Join-Path $workspace "x64/Release/$_.lib" }
    $systemLibraries = @('dbghelp.lib','winmm.lib','setupapi.lib','Imm32.lib','Version.lib','Avrt.lib','user32.lib','gdi32.lib','shell32.lib','ole32.lib','oleaut32.lib','advapi32.lib','uuid.lib')
    $testSources = @(Get-ChildItem $PSScriptRoot -Filter '*Test.cpp' | Where-Object { $_.BaseName -like $Test })
    if (!$testSources.Count) { throw "No tests match '$Test'." }
    foreach($testSource in $testSources) {
        $name = $testSource.BaseName
        $objectDirectory = Join-Path $outDirectory $name
        New-Item -ItemType Directory -Force -Path $objectDirectory | Out-Null
        $options = @('/nologo','/EHsc','/MD','/O2','/DUSE_SDL2','/DGLEW_STATIC','/D_NEWTON_USE_LIB','/DIL_STATIC_LIB','/DHAVE_LIBC','/DAL_LIBTYPE_STATIC',"/I$workspace/HPL2/core/include","/I$workspace/HPL2/dependencies/include",$testSource.FullName,(Join-Path $PSScriptRoot 'EngineEntry.cpp'),"/Fo$objectDirectory/","/Fe$outDirectory/$name.exe")
        & "$toolset/bin/Hostx64/x64/cl.exe" @options @libraries /link /LTCG /SUBSYSTEM:CONSOLE /INCREMENTAL:NO @systemLibraries
        if ($LASTEXITCODE -ne 0) { throw "$name compilation failed." }
        Push-Location $outDirectory
        try {
            & (Join-Path $outDirectory "$name.exe")
            if ($LASTEXITCODE -ne 0) { throw "$name failed." }
        } finally { Pop-Location }
    }
} finally {
    $env:INCLUDE = $previousInclude
    $env:LIB = $previousLib
}
