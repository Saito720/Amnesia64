param([switch]$Live, [string]$WorkingDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../../tests/multiplayer/TestSupport.ps1')
$context = Get-MultiplayerTestContext -Kind game
$workspace = $context.Workspace
$output = Join-Path $workspace 'bld/steam-transport-tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$includeDirs = @((Join-Path $context.Toolset 'include'), (Join-Path $context.Sdk "Include/$($context.SdkVersion)/ucrt"), (Join-Path $context.Sdk "Include/$($context.SdkVersion)/shared"), (Join-Path $context.Sdk "Include/$($context.SdkVersion)/um"))
$libDirs = @((Join-Path $context.Toolset 'lib/x64'), (Join-Path $context.Sdk "Lib/$($context.SdkVersion)/ucrt/x64"), (Join-Path $context.Sdk "Lib/$($context.SdkVersion)/um/x64"))
$sdk = Join-Path $workspace 'HPL2/dependencies/steamworks/sdk'
$appid = [IO.File]::ReadAllText((Join-Path $workspace 'steam_appid.txt')).Trim()
if($appid -notmatch '^[1-9][0-9]*$') { throw 'Invalid repository steam_appid.txt' }
$oldInclude = $env:INCLUDE
$oldLib = $env:LIB
try {
    $env:INCLUDE = $includeDirs -join ';'
    $env:LIB = $libDirs -join ';'
    Push-Location $output
    try {
        & $context.Compiler /nologo /EHsc /std:c++14 /MD /W4 /WX (Join-Path $PSScriptRoot 'SteamValidationTest.cpp') /Fe:SteamValidationTest.exe
        if($LASTEXITCODE -ne 0) { throw 'Steam metadata test build failed.' }
        & (Join-Path $output 'SteamValidationTest.exe')
        if($LASTEXITCODE -ne 0) { throw 'Steam metadata tests failed.' }
        foreach($test in @('SteamTransportTest','NetworkTransportTest')) {
            & $context.Compiler /nologo /EHsc /std:c++14 /MD /W3 /D_CRT_SECURE_NO_WARNINGS /DHPL_USE_STEAMWORKS "/DHPL_STEAM_APP_ID=$appid" "/I$(Join-Path $workspace 'HPL2/core/include')" "/I$(Join-Path $sdk 'public')" (Join-Path $PSScriptRoot "$test.cpp") (Join-Path $workspace 'HPL2/core/sources/network/NetworkTransport.cpp') "/Fe:$test.exe" /link (Join-Path $sdk 'redistributable_bin/win64/steam_api64.lib') ws2_32.lib
            if($LASTEXITCODE -ne 0) { throw "$test build failed." }
        }
        Copy-Item -LiteralPath (Join-Path $sdk 'redistributable_bin/win64/steam_api64.dll') -Destination $output -Force
        Copy-Item -LiteralPath (Join-Path $workspace 'steam_appid.txt') -Destination $output -Force
    } finally { Pop-Location }
} finally { $env:INCLUDE = $oldInclude; $env:LIB = $oldLib }
if($Live) {
    if(-not $WorkingDirectory) { $WorkingDirectory = $workspace }
    # Friends-only lobby lifecycle test. It never opens the invite dialog, sends
    # an invitation, publishes a public lobby, or alters Steam partner settings.
    foreach($test in @('SteamTransportTest','NetworkTransportTest')) {
        $stdout = Join-Path $output "$test.stdout.log"
        $stderr = Join-Path $output "$test.stderr.log"
        $process = Start-Process -FilePath (Join-Path $output "$test.exe") -WorkingDirectory $WorkingDirectory -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        if(-not $process.WaitForExit(100000)) { Stop-Process -Id $process.Id; throw "$test timed out." }
        Get-Content -LiteralPath $stdout
        Get-Content -LiteralPath $stderr
        if($process.ExitCode -ne 0) { throw "$test failed with exit $($process.ExitCode)." }
    }
}
