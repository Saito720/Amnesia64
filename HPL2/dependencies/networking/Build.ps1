param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateSet('Win32', 'x64')][string]$Platform = 'x64',
    [string]$Toolset = 'v145',
    [string]$CMake = 'cmake.exe',
    [string]$VisualStudioInstance = '',
    [string]$OutputDirectory = '',
    [ValidateSet('true','false')][string]$UseSteamworks = 'false',
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if ($UseSteamworks -eq 'true') {
    # The SDK is prebuilt. The game deploys its matching runtime after linking.
    if (!(Test-Path -LiteralPath (Join-Path $PSScriptRoot '../steamworks/sdk/public/steam/steam_api.h'))) {
        throw 'The Steamworks SDK is missing. Restore it or set HplUseSteamworks=false.'
    }
    Write-Output 'Steamworks backend selected; standalone GameNetworkingSockets is not built or linked.'
    exit 0
}
$buildDirectory = Join-Path $repository "bld/networking/$Toolset/$Platform"
if ($Clean) {
    if (Test-Path (Join-Path $buildDirectory 'CMakeCache.txt')) {
        & $CMake --build $buildDirectory --config $Configuration --target clean
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    exit 0
}
$configure = @('-S', $PSScriptRoot, '-B', $buildDirectory, '-G', 'Visual Studio 18 2026', '-A', $Platform, '-T', $Toolset, '-DHPL_USE_STEAMWORKS=OFF')
if ($VisualStudioInstance) { $configure += "-DCMAKE_GENERATOR_INSTANCE=$VisualStudioInstance" }
& $CMake @configure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $CMake --build $buildDirectory --config $Configuration --target GameNetworkingSockets_s --parallel 8
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $repository $(if ($Platform -eq 'x64') { "x64/$Configuration" } else { $Configuration })
}
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$protobufName = if ($Configuration -eq 'Debug') { 'libprotobufd.lib' } else { 'libprotobuf.lib' }
Copy-Item -LiteralPath (Join-Path $buildDirectory "gns/src/$Configuration/GameNetworkingSockets_s.lib") -Destination (Join-Path $OutputDirectory 'GameNetworkingSockets_s.lib') -Force
Copy-Item -LiteralPath (Join-Path $buildDirectory "protobuf/$Configuration/$protobufName") -Destination (Join-Path $OutputDirectory 'HPLProtobuf.lib') -Force
