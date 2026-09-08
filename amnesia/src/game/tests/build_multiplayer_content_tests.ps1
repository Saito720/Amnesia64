param([string]$RetailDirectory = '', [switch]$Run)
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$output = Join-Path $repository 'bld/multiplayer-content-tests'
New-Item -ItemType Directory -Force $output | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$studio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$studio) { throw 'Visual Studio C++ tools are required.' }
$toolset = (Get-ChildItem -LiteralPath (Join-Path $studio 'VC/Tools/MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).FullName
$sdk = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10
$sdkVersion = (Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'um/Windows.h') } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
$env:INCLUDE = "$toolset/include;$sdk/Include/$sdkVersion/ucrt;$sdk/Include/$sdkVersion/shared;$sdk/Include/$sdkVersion/um;$sdk/Include/$sdkVersion/winrt"
$env:LIB = "$toolset/lib/x64;$sdk/Lib/$sdkVersion/ucrt/x64;$sdk/Lib/$sdkVersion/um/x64"
$libraries = @('HPL2','Newton','AngelScript','DevIL','freealut','GLEW','jpeg','ogg','OpenAL32','png','SDL2','theora','vorbis','zlib') | ForEach-Object { Join-Path $repository "x64/Debug/$_.lib" }
$options = @('/nologo','/EHsc','/std:c++17','/MDd','/Od','/Zi','/D_DEBUG','/DMEMORY_MANAGER_ACTIVE','/DUSE_SDL2','/DUSE_GAMEPAD','/DGLEW_STATIC','/D_NEWTON_USE_LIB','/DIL_STATIC_LIB','/DHAVE_LIBC','/DAL_LIBTYPE_STATIC',"/I$repository/HPL2/core/include","/I$repository/HPL2/dependencies/include","/I$repository/amnesia/src/game",(Join-Path $PSScriptRoot 'multiplayer_content_tests.cpp'),"/Fo$output/","/Fd$output/harness.pdb","/Fe$output/multiplayer-content-tests.exe")
& "$toolset/bin/Hostx64/x64/cl.exe" @options @libraries /link /SUBSYSTEM:CONSOLE /INCREMENTAL:NO opengl32.lib dbghelp.lib winmm.lib setupapi.lib Imm32.lib Version.lib Avrt.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib uuid.lib
if ($LASTEXITCODE -ne 0) { throw 'Map content test build failed. Build Amnesia Debug x64 first.' }
if ($Run) {
    if (!$RetailDirectory) { $RetailDirectory = [Environment]::GetEnvironmentVariable('ATDD_DIR','User') }
    if (!(Test-Path (Join-Path $RetailDirectory 'resources.cfg'))) { throw 'Provide the retail Amnesia directory.' }
    $log = Join-Path $output 'hpl.log'
    $process = Start-Process -FilePath (Join-Path $output 'multiplayer-content-tests.exe') -ArgumentList ('"'+$log+'"') -WorkingDirectory $RetailDirectory -WindowStyle Hidden -RedirectStandardOutput (Join-Path $output 'stdout.log') -RedirectStandardError (Join-Path $output 'stderr.log') -PassThru
    if (!$process.WaitForExit(60000)) { Stop-Process -Id $process.Id; throw 'Map content tests timed out.' }
    Get-Content (Join-Path $output 'stdout.log')
    Get-Content (Join-Path $output 'stderr.log')
    if ($process.ExitCode -ne 0) { throw 'Map content tests failed.' }
}
