param(
    [ValidateSet('ui','game')][string]$Kind='game',
    [ValidateSet('Standalone','Steamworks')][string]$Backend='Standalone',
    [switch]$SkipGameBuild
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'TestSupport.ps1')
$context = Get-MultiplayerTestContext $Kind
$workspace = $context.Workspace
$steamEnabled = if($Backend -eq 'Steamworks') { 'true' } else { 'false' }
if(-not $SkipGameBuild) {
    & $context.MSBuild (Join-Path $workspace 'amnesia/src/game/Lux.vcxproj') '/p:Configuration=Debug' '/p:Platform=x64' "/p:HplUseSteamworks=$steamEnabled" ("/p:SolutionDir=$workspace\") ("/p:WindowsTargetPlatformVersion="+$context.SdkVersion) /m /v:minimal /nologo
    if($LASTEXITCODE -ne 0) { throw 'Build the Debug x64 game successfully before running these tests.' }
}
$savedInclude=$env:INCLUDE
$savedLib=$env:LIB
try {
    $toolset=$context.Toolset; $sdk=$context.Sdk; $sdkVersion=$context.SdkVersion
    $env:INCLUDE="$toolset/include;$sdk/Include/$sdkVersion/ucrt;$sdk/Include/$sdkVersion/shared;$sdk/Include/$sdkVersion/um;$sdk/Include/$sdkVersion/winrt"
    $env:LIB="$toolset/lib/x64;$sdk/Lib/$sdkVersion/ucrt/x64;$sdk/Lib/$sdkVersion/um/x64"
    $libDirectory=Join-Path $workspace 'x64/Debug'
    $backendFile=Join-Path $libDirectory 'hpl-networking-backend.txt'
    if(-not (Test-Path -LiteralPath $backendFile) -or [IO.File]::ReadAllText($backendFile).Trim() -ne $steamEnabled) {
        throw "The existing Debug game objects do not match backend $Backend. Run this script without -SkipGameBuild."
    }
    $libraryNames=@('HPL2','Newton','AngelScript','DevIL','freealut','GLEW','jpeg','ogg','OpenAL32','png','SDL2','theora','vorbis','zlib')
    $additionalSources=@()
    $objects=@()
    if($Kind -eq 'game') {
        if($Backend -eq 'Standalone') { $libraryNames += @('GameNetworkingSockets_s','HPLProtobuf') }
        [xml]$project=[IO.File]::ReadAllText((Join-Path $workspace 'amnesia/src/game/Lux.vcxproj'))
        $objects=@($project.GetElementsByTagName('ClCompile') | Where-Object { $_.HasAttribute('Include') -and $_.Include -ne 'Main.cpp' } | ForEach-Object {
            Join-Path $workspace ('amnesia/src/game/x64/Debug/'+[IO.Path]::GetFileNameWithoutExtension($_.Include)+'.obj')
        })
    } else {
        $inputSource=[IO.File]::ReadAllText((Join-Path $workspace 'amnesia/src/game/LuxInputHandler.cpp'))
        $start=$inputSource.IndexOf('void cLuxInputHandler::Update(float afTimeStep)')
        $end=$inputSource.IndexOf('void cLuxInputHandler::Reset()', $start)
        if($start -lt 0 -or $end -lt 0) { throw 'Could not locate the production input Update method.' }
        [IO.File]::WriteAllText((Join-Path $context.Output 'generated_update.cpp'),$inputSource.Substring($start,$end-$start))
        $additionalSources=@('imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','backends/imgui_impl_sdl2.cpp','backends/imgui_impl_opengl3.cpp') | ForEach-Object {
            Join-Path $workspace "HPL2/dependencies/sources/imgui/$_"
        }
    }
    $libraries=$libraryNames | ForEach-Object { Join-Path $libDirectory "$_.lib" }
    $backendOptions=@()
    if($Backend -eq 'Steamworks') {
        $steamSdk=Join-Path $workspace 'HPL2/dependencies/steamworks/sdk'
        $appId=[IO.File]::ReadAllText((Join-Path $workspace 'steam_appid.txt')).Trim()
        if($appId -notmatch '^[1-9][0-9]{0,9}$') { throw 'A numeric root steam_appid.txt is required.' }
        $backendOptions=@('/DHPL_USE_STEAMWORKS',"/DHPL_STEAM_APP_ID=$appId","/I$steamSdk/public")
        $libraries += Join-Path $steamSdk 'redistributable_bin/win64/steam_api64.lib'
        Copy-Item -LiteralPath (Join-Path $steamSdk 'redistributable_bin/win64/steam_api64.dll') -Destination $context.Output -Force
        Copy-Item -LiteralPath (Join-Path $workspace 'steam_appid.txt') -Destination $context.Output -Force
    }
    $options=@('/nologo','/EHsc','/std:c++17','/MDd','/Od','/Zi','/D_DEBUG','/DMEMORY_MANAGER_ACTIVE','/DUSE_SDL2','/DUSE_GAMEPAD','/DGLEW_STATIC','/D_NEWTON_USE_LIB','/DIL_STATIC_LIB','/DHAVE_LIBC','/DAL_LIBTYPE_STATIC',
        "/I$workspace/HPL2/core/include","/I$workspace/HPL2/dependencies/include","/I$workspace/HPL2/dependencies/include/SDL2","/I$workspace/HPL2/dependencies/sources/imgui","/I$workspace/amnesia/src/game",("/I"+$context.Output),
        (Join-Path $PSScriptRoot "$Kind.cpp"),("/Fo"+$context.Output+'/'),("/Fd"+$context.Output+'/harness.pdb'),("/Fe"+$context.Output+'/smoke.exe'))
    & $context.Compiler @backendOptions @options @additionalSources @objects @libraries /link /SUBSYSTEM:CONSOLE /INCREMENTAL:NO opengl32.lib ws2_32.lib crypt32.lib bcrypt.lib Iphlpapi.lib dbghelp.lib winmm.lib setupapi.lib Imm32.lib Version.lib Avrt.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib uuid.lib
    if($LASTEXITCODE -ne 0) { throw "$Kind harness build failed." }
} finally {
    $env:INCLUDE=$savedInclude
    $env:LIB=$savedLib
}
