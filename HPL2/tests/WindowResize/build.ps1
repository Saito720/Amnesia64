param(
    [ValidateSet('x64', 'x86')][string]$Architecture = 'x64',
    [switch]$BuildOnly,
    [switch]$Fullscreen,
    [switch]$Engine,
    [string]$GameDirectory = $env:ATDD_DIR
)

$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio C++ tools were not found.' }
$toolset = Get-ChildItem -Directory (Join-Path $visualStudio 'VC/Tools/MSVC') |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1 -ExpandProperty FullName
$sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdkVersion = Get-ChildItem -Directory (Join-Path $sdk 'Include') |
    Where-Object { Test-Path (Join-Path $_.FullName 'um/Windows.h') } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1 -ExpandProperty Name
if (-not $sdkVersion) { throw 'A Windows SDK was not found.' }

$libDirectory = if ($Architecture -eq 'x64') { Join-Path $workspace 'x64/Debug' } else { Join-Path $workspace 'Debug' }
$outDirectory = Join-Path $workspace "bld/window-resize-test/$Architecture"
New-Item -ItemType Directory -Force -Path $outDirectory | Out-Null
$libraries = @('HPL2', 'Newton', 'AngelScript', 'DevIL', 'freealut', 'GLEW', 'jpeg', 'ogg', 'OpenAL32', 'png', 'SDL2', 'theora', 'vorbis', 'zlib') |
    ForEach-Object { Join-Path $libDirectory "$_.lib" }
foreach ($library in $libraries) {
    if (-not (Test-Path -LiteralPath $library)) { throw "Build the Debug solution first. Missing library: $library" }
}

$oldInclude = $env:INCLUDE
$oldLib = $env:LIB
try {
    $env:INCLUDE = "$toolset/include;$sdk/Include/$sdkVersion/ucrt;$sdk/Include/$sdkVersion/shared;$sdk/Include/$sdkVersion/um;$sdk/Include/$sdkVersion/winrt"
    $env:LIB = "$libDirectory;$toolset/lib/$Architecture;$sdk/Lib/$sdkVersion/ucrt/$Architecture;$sdk/Lib/$sdkVersion/um/$Architecture"
    $options = @(
        '/nologo', '/EHsc', '/MDd', '/Od', '/Zi', '/D_DEBUG', '/DMEMORY_MANAGER_ACTIVE',
        '/DUSE_SDL2', '/DUSE_GAMEPAD', '/DGLEW_STATIC', '/D_NEWTON_USE_LIB', '/DIL_STATIC_LIB',
        '/DHAVE_LIBC', '/DAL_LIBTYPE_STATIC',
        "/I$workspace/HPL2/core/include", "/I$workspace/HPL2/dependencies/include",
        (Join-Path $PSScriptRoot 'WindowResize.cpp'),
        "/Fo$outDirectory/WindowResize.obj", "/Fd$outDirectory/WindowResize.pdb",
        "/Fe$outDirectory/window-resize-test.exe"
    )
    & "$toolset/bin/Hostx64/$Architecture/cl.exe" @options @libraries /link /SUBSYSTEM:CONSOLE /INCREMENTAL:NO dbghelp.lib winmm.lib setupapi.lib Imm32.lib Version.lib Avrt.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib uuid.lib
    if ($LASTEXITCODE -ne 0) { throw 'Window resize smoke test compilation failed.' }
} finally {
    $env:INCLUDE = $oldInclude
    $env:LIB = $oldLib
}

if (-not $BuildOnly) {
    if ($Engine) {
        $coreAssets = Join-Path $GameDirectory 'core'
        if (-not (Test-Path -LiteralPath $coreAssets)) {
            throw 'The full engine test needs -GameDirectory or ATDD_DIR pointing to an installed game.'
        }
        Copy-Item -LiteralPath $coreAssets -Destination $outDirectory -Recurse -Force
    }
    $launch = @{
        FilePath = Join-Path $outDirectory 'window-resize-test.exe'
        WorkingDirectory = $outDirectory
        WindowStyle = 'Hidden'
        RedirectStandardOutput = Join-Path $outDirectory 'stdout.log'
        RedirectStandardError = Join-Path $outDirectory 'stderr.log'
        PassThru = $true
    }
    $testArguments = @()
    if ($Fullscreen) { $testArguments += '--fullscreen' }
    if ($Engine) { $testArguments += '--engine' }
    if ($testArguments.Count -gt 0) { $launch.ArgumentList = $testArguments }
    $process = Start-Process @launch
    if (-not $process.WaitForExit(45000)) {
        $process.Kill()
        throw 'Window resize smoke test exceeded 45 seconds.'
    }
    Get-Content -LiteralPath $launch.RedirectStandardOutput
    Get-Content -LiteralPath $launch.RedirectStandardError
    if ($process.ExitCode -ne 0) { throw "Window resize smoke test failed with exit code $($process.ExitCode)." }
}
