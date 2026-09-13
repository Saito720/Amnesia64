param([string]$RetailDirectory)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../multiplayer/TestSupport.ps1')
$context=Get-MultiplayerTestContext game
$repo=$context.Workspace
$RetailDirectory=Find-AmnesiaRetailDirectory $RetailDirectory
$savedInclude=$env:INCLUDE; $savedLib=$env:LIB
try {
    $env:INCLUDE="$($context.Toolset)/include;$($context.Sdk)/Include/$($context.SdkVersion)/ucrt;$($context.Sdk)/Include/$($context.SdkVersion)/shared;$($context.Sdk)/Include/$($context.SdkVersion)/um"
    $env:LIB="$($context.Toolset)/lib/x64;$($context.Sdk)/Lib/$($context.SdkVersion)/ucrt/x64;$($context.Sdk)/Lib/$($context.SdkVersion)/um/x64"
    $libraries=@('HPL2','Newton','AngelScript','DevIL','freealut','GLEW','jpeg','ogg','OpenAL32','png','SDL2','theora','vorbis','zlib') | ForEach-Object { "$repo/x64/Debug/$_.lib" }
    foreach($kind in @('ModelEditor','LevelEditor','ParticleEditor','MaterialEditor')) {
        $directory=Join-Path $repo ('HPL2/tools/editors/'+$kind.ToLowerInvariant())
        $output=Join-Path $repo "bld/editor-resize-tests/$kind"
        New-Item -ItemType Directory -Path $output -Force | Out-Null
        [xml]$project=[IO.File]::ReadAllText("$directory/$kind.vcxproj")
        $objects=@($project.GetElementsByTagName('ClCompile') | Where-Object { $_.HasAttribute('Include') -and $_.Include -ne "${kind}Main.cpp" } | ForEach-Object { "$directory/x64/Debug/$([IO.Path]::GetFileNameWithoutExtension($_.Include)).obj" })
        $extra=@(); if($kind -eq 'MaterialEditor') { $extra += '/DMATERIAL_EDITOR_TEST' }
        & $context.Compiler @extra /nologo /EHsc /std:c++17 /MDd /D_DEBUG /DMEMORY_MANAGER_ACTIVE /DUSE_SDL2 /DUSE_GAMEPAD /DGLEW_STATIC /D_NEWTON_USE_LIB /DIL_STATIC_LIB /DHAVE_LIBC /DAL_LIBTYPE_STATIC "/DEDITOR_CLASS=c$kind" ('/DEDITOR_HEADER="'+$kind+'.h"') "/I$directory" "/I$repo/HPL2/tools/editors/common" "/I$repo/HPL2/core/include" "/I$repo/HPL2/dependencies/include" "$PSScriptRoot/resize.cpp" "/Fo$output/resize.obj" "/Fe$output/resize.exe" @objects @libraries /link /INCREMENTAL:NO /SUBSYSTEM:CONSOLE opengl32.lib dbghelp.lib winmm.lib setupapi.lib Imm32.lib Version.lib Avrt.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib uuid.lib ws2_32.lib comctl32.lib
        if($LASTEXITCODE) { throw "$kind resize test build failed" }
        $process=Start-Process -FilePath "$output/resize.exe" -ArgumentList ('"'+$output+'"') -WorkingDirectory $RetailDirectory -WindowStyle Hidden -RedirectStandardOutput "$output/stdout.log" -RedirectStandardError "$output/stderr.log" -PassThru
        if(!$process.WaitForExit(60000)) { Stop-Process -Id $process.Id; throw "$kind resize test timed out" }
        Get-Content "$output/stdout.log"
        Get-Content "$output/stderr.log"
        if($process.ExitCode) { throw "$kind resize test failed" }
    }
} finally { $env:INCLUDE=$savedInclude; $env:LIB=$savedLib }
