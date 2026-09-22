param(
    [Parameter(Mandatory = $true)][string]$FontPath,
    [string]$GameDirectory = $env:ATDD_DIR,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$font = [IO.Path]::GetFullPath($FontPath)
if (-not (Test-Path -LiteralPath $font -PathType Leaf)) { throw "Font file not found: $font" }
if (-not $GameDirectory) { throw 'Supply -GameDirectory or set ATDD_DIR.' }
$core = Join-Path $GameDirectory 'core'
if (-not (Test-Path -LiteralPath $core -PathType Container)) {
    throw 'An installed game core directory is required. Supply -GameDirectory or set ATDD_DIR.'
}

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

$libDirectory = Join-Path $workspace 'x64/Debug'
if (-not $SkipBuild) {
    & (Join-Path $visualStudio 'MSBuild/Current/Bin/MSBuild.exe') (Join-Path $workspace 'HPL2/core/HPL2.vcxproj') /m /nologo /v:minimal /p:Configuration=Debug /p:Platform=x64 /p:HplUseSteamworks=false "/p:SolutionDir=$workspace\" "/p:WindowsTargetPlatformVersion=$sdkVersion"
    if ($LASTEXITCODE -ne 0) { throw 'Engine build failed.' }
}
$outDirectory = Join-Path $workspace 'bld/font-rendering-test/x64'
New-Item -ItemType Directory -Force -Path $outDirectory | Out-Null
$libraries = @('HPL2', 'Newton', 'AngelScript', 'DevIL', 'freealut', 'GLEW', 'jpeg', 'ogg', 'OpenAL32', 'png', 'SDL2', 'theora', 'vorbis', 'zlib') |
    ForEach-Object { Join-Path $libDirectory "$_.lib" }
foreach ($library in $libraries) {
    if (-not (Test-Path -LiteralPath $library)) { throw "Build the Debug x64 solution first. Missing library: $library" }
}

$oldInclude = $env:INCLUDE
$oldLib = $env:LIB
try {
    $env:INCLUDE = "$toolset/include;$sdk/Include/$sdkVersion/ucrt;$sdk/Include/$sdkVersion/shared;$sdk/Include/$sdkVersion/um;$sdk/Include/$sdkVersion/winrt"
    $env:LIB = "$libDirectory;$toolset/lib/x64;$sdk/Lib/$sdkVersion/ucrt/x64;$sdk/Lib/$sdkVersion/um/x64"
    $options = @(
        '/nologo', '/EHsc', '/utf-8', '/MDd', '/Od', '/Zi', '/D_DEBUG', '/DMEMORY_MANAGER_ACTIVE',
        '/DUSE_SDL2', '/DUSE_GAMEPAD', '/DGLEW_STATIC', '/D_NEWTON_USE_LIB', '/DIL_STATIC_LIB',
        '/DHAVE_LIBC', '/DAL_LIBTYPE_STATIC',
        "/I$workspace/HPL2/core/include", "/I$workspace/HPL2/dependencies/include",
        (Join-Path $PSScriptRoot 'FontRendering.cpp'),
        "/Fo$outDirectory/FontRendering.obj", "/Fd$outDirectory/FontRendering.pdb",
        "/Fe$outDirectory/font-rendering-test.exe"
    )
    & "$toolset/bin/Hostx64/x64/cl.exe" @options @libraries /link /SUBSYSTEM:CONSOLE /INCREMENTAL:NO dbghelp.lib winmm.lib setupapi.lib Imm32.lib Version.lib Avrt.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib uuid.lib
    if ($LASTEXITCODE -ne 0) { throw 'Font rendering test compilation failed.' }
} finally {
    $env:INCLUDE = $oldInclude
    $env:LIB = $oldLib
}

Copy-Item -LiteralPath $core -Destination $outDirectory -Recurse -Force
$retailFonts = Join-Path $GameDirectory 'fonts/eng'
$stagedBitmapFonts = Join-Path $outDirectory 'retail-fonts'
New-Item -ItemType Directory -Force -Path $stagedBitmapFonts | Out-Null
Get-ChildItem -LiteralPath $retailFonts -File | Where-Object { $_.Extension -in '.fnt', '.dds' } |
    Copy-Item -Destination $stagedBitmapFonts -Force
$stagedFont = Join-Path $outDirectory 'FontRenderingTest.ttf'
Copy-Item -LiteralPath $font -Destination $stagedFont -Force
$launch = @{
    FilePath = Join-Path $outDirectory 'font-rendering-test.exe'
    WorkingDirectory = $outDirectory
    WindowStyle = 'Hidden'
    RedirectStandardOutput = Join-Path $outDirectory 'stdout.log'
    RedirectStandardError = Join-Path $outDirectory 'stderr.log'
    PassThru = $true
}
try {
    $process = Start-Process @launch
    if (-not $process.WaitForExit(45000)) {
        $process.Kill()
        $null = $process.WaitForExit(5000)
        throw 'Font rendering test exceeded 45 seconds.'
    }
    Get-Content -LiteralPath $launch.RedirectStandardOutput
    Get-Content -LiteralPath $launch.RedirectStandardError
    if ($process.ExitCode -ne 0) { throw "Font rendering test failed with exit code $($process.ExitCode)." }
} finally {
    Remove-Item -LiteralPath $stagedFont -ErrorAction SilentlyContinue
}
