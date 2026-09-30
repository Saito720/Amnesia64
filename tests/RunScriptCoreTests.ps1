param([string]$Compiler = '')
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputDirectory = Join-Path $workspace 'bld/script-core-tests'
$asPath = Join-Path $workspace 'HPL2/dependencies/sources/AngelScript'
$asObjects = Join-Path $outputDirectory 'as-obj'
$asLibs = Join-Path $outputDirectory 'lib'
New-Item -ItemType Directory -Force $asObjects,$asLibs | Out-Null
$savedInclude = $env:INCLUDE
$savedLib = $env:LIB
try {
    if (!$Compiler) {
        $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
        $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (!$installation) { throw 'Visual Studio C++ tools were not found.' }
        $toolset = Get-ChildItem -LiteralPath (Join-Path $installation 'VC/Tools/MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
        $Compiler = Join-Path $toolset.FullName 'bin/Hostx64/x64/cl.exe'
        $sdk = "${env:ProgramFiles(x86)}/Windows Kits/10"
        $sdkVersion = Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'um/Windows.h') } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
        if (!$sdkVersion) { throw 'A Windows SDK is required.' }
        $sdkVersion = $sdkVersion.Name
        $env:INCLUDE = "$($toolset.FullName)/include;$sdk/Include/$sdkVersion/ucrt;$sdk/Include/$sdkVersion/shared;$sdk/Include/$sdkVersion/um;$sdk/Include/$sdkVersion/winrt"
        $env:LIB = "$($toolset.FullName)/lib/x64;$sdk/Lib/$sdkVersion/ucrt/x64;$sdk/Lib/$sdkVersion/um/x64"
    }
    $compilerDirectory = Split-Path $Compiler
    # Build the changed VM into a private archive without replacing game output.
    [xml]$asProject = [IO.File]::ReadAllText((Join-Path $asPath 'AngelScript.vcxproj'))
    $asSources = $asProject.GetElementsByTagName('ClCompile') | Where-Object { $_.HasAttribute('Include') } | ForEach-Object { Join-Path $asPath $_.Include }
    & $Compiler /nologo /c /EHsc /MDd /Od /Z7 /D_DEBUG /D_LIB "/I$asPath/include" "/Fo$asObjects/" @asSources
    if ($LASTEXITCODE -ne 0) { throw 'AngelScript compile failed.' }
    & "$compilerDirectory/ml64.exe" /nologo /c "/Fo$asObjects/as_callfunc_x64_msvc_asm.obj" (Join-Path $asPath 'sources/as_callfunc_x64_msvc_asm.asm')
    if ($LASTEXITCODE -ne 0) { throw 'AngelScript assembler failed.' }
    $asObjectFiles = $asSources | ForEach-Object { Join-Path $asObjects ([IO.Path]::GetFileNameWithoutExtension($_)+'.obj') }
    & "$compilerDirectory/lib.exe" /nologo "/OUT:$asLibs/AngelScript.lib" @asObjectFiles (Join-Path $asObjects 'as_callfunc_x64_msvc_asm.obj')
    if ($LASTEXITCODE -ne 0) { throw 'AngelScript archive failed.' }
    $options = @('/nologo','/EHsc','/std:c++17','/MDd','/Od','/Z7','/D_DEBUG','/DMEMORY_MANAGER_ACTIVE','/DUSE_SDL2','/DUSE_GAMEPAD','/DGLEW_STATIC','/D_NEWTON_USE_LIB','/DIL_STATIC_LIB','/DHAVE_LIBC','/DAL_LIBTYPE_STATIC',"/I$workspace/HPL2/core/include","/I$workspace/HPL2/dependencies/include","/I$asPath/include")
    & $Compiler @options /c (Join-Path $workspace 'HPL2/core/sources/impl/SqScript.cpp') "/Fo$outputDirectory/SqScript.obj"
    if ($LASTEXITCODE -ne 0) { throw 'SqScript compile failed.' }
    & $Compiler @options /DIGNORE_HPL_MAIN /c (Join-Path $workspace 'HPL2/core/sources/impl/LowLevelSystemSDL.cpp') "/Fo$outputDirectory/LowLevelSystemSDL.obj"
    if ($LASTEXITCODE -ne 0) { throw 'Script diagnostic support compile failed.' }
    # Existing Debug x64 engine/dependency libraries are required; no retail assets.
    $libDirectory = Join-Path $workspace 'x64/Debug'
    $libs = @('HPL2','Newton','DevIL','freealut','GLEW','jpeg','ogg','OpenAL32','png','SDL2','theora','vorbis','zlib') | ForEach-Object { Join-Path $libDirectory "$_.lib" }
    $backendFile = Join-Path $libDirectory 'hpl-networking-backend.txt'
    if ((Test-Path -LiteralPath $backendFile) -and [IO.File]::ReadAllText($backendFile).Trim() -eq 'true') {
        $steamSdk = Join-Path $workspace 'HPL2/dependencies/steamworks/sdk'
        $libs += Join-Path $steamSdk 'redistributable_bin/win64/steam_api64.lib'
        Copy-Item -LiteralPath (Join-Path $steamSdk 'redistributable_bin/win64/steam_api64.dll') -Destination $outputDirectory -Force
    } else {
        $libs += @('GameNetworkingSockets_s','HPLProtobuf') | ForEach-Object { Join-Path $libDirectory "$_.lib" }
    }
    & $Compiler @options (Join-Path $PSScriptRoot 'script_core_tests.cpp') "/Fo$outputDirectory/harness.obj" "/Fe$outputDirectory/script-core-tests.exe" (Join-Path $outputDirectory 'SqScript.obj') (Join-Path $outputDirectory 'LowLevelSystemSDL.obj') @libs (Join-Path $asLibs 'AngelScript.lib') /link "/LIBPATH:$asLibs" /SUBSYSTEM:CONSOLE /INCREMENTAL:NO opengl32.lib ws2_32.lib crypt32.lib bcrypt.lib Iphlpapi.lib dbghelp.lib winmm.lib setupapi.lib Imm32.lib Version.lib Avrt.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib uuid.lib
    if ($LASTEXITCODE -ne 0) { throw 'Script core harness build failed.' }
    & "$outputDirectory/script-core-tests.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Script core tests failed.' }
} finally {
    $env:INCLUDE = $savedInclude
    $env:LIB = $savedLib
}
