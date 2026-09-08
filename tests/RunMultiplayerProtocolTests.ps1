param([string]$Compiler = '')
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputDirectory = Join-Path $workspace 'bld/multiplayer-protocol-tests'
New-Item -ItemType Directory -Force $outputDirectory | Out-Null
if (!$Compiler) {
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$installation) { throw 'Visual Studio C++ tools were not found.' }
    $toolsetVersion = (Get-Content (Join-Path $installation 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt')).Trim()
    $Compiler = Join-Path $installation "VC/Tools/MSVC/$toolsetVersion/bin/Hostx64/x64/cl.exe"
    $toolsetRoot = [IO.Path]::GetFullPath((Join-Path (Split-Path $Compiler) '../../..'))
    $sdkRoot = "${env:ProgramFiles(x86)}/Windows Kits/10"
    $sdkVersion = (Get-ChildItem "$sdkRoot/Include" -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
    $env:INCLUDE = "$toolsetRoot/include;$sdkRoot/Include/$sdkVersion/ucrt;$sdkRoot/Include/$sdkVersion/shared;$sdkRoot/Include/$sdkVersion/um"
    $env:LIB = "$toolsetRoot/lib/x64;$sdkRoot/Lib/$sdkVersion/ucrt/x64;$sdkRoot/Lib/$sdkVersion/um/x64"
}
foreach ($test in @('multiplayer_session_protocol_tests','multiplayer_world_protocol_tests','steam_launch_tests')) {
    & $Compiler /nologo /EHsc /std:c++17 /W4 /MD "$PSScriptRoot/$test.cpp" "/Fo$outputDirectory/$test.obj" "/Fe$outputDirectory/$test.exe"
    if ($LASTEXITCODE -ne 0) { throw "$test failed to compile." }
    & "$outputDirectory/$test.exe"
    if ($LASTEXITCODE -ne 0) { throw "$test failed." }
}
