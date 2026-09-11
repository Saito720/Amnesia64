param([string]$RetailDirectory,[int]$Port=27843,[ValidateSet('Standalone','Steamworks')][string]$Backend='Standalone',[switch]$SteamHostOnly,[switch]$SkipBuild,[switch]$KeepProfiles)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'TestSupport.ps1')
$context=Get-MultiplayerTestContext 'game'
$retail=Find-AmnesiaRetailDirectory $RetailDirectory
if($SteamHostOnly -and $Backend -ne 'Steamworks') { throw '-SteamHostOnly requires -Backend Steamworks and a signed-in account with access to the configured AppID.' }
$roles=if($SteamHostOnly) { @('steam-host') } else { @('host','client') }
if(-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -Kind game -Backend $Backend }
if($Port -lt 1 -or $Port -gt 65535) { throw 'Port must be from 1 to 65535.' }
$runId=[Guid]::NewGuid().ToString('N').Substring(0,12)
$run=Join-Path $context.Output $runId
New-Item -ItemType Directory -Path $run | Out-Null
$profileParent=[IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Amnesia'))
$profilePaths=@{}
$template=[IO.File]::ReadAllText((Join-Path $retail 'config/main_init.cfg'))
$mainPath=(Join-Path $run 'main-settings.cfg').Replace('\','/')
[IO.File]::WriteAllText($mainPath,@'
<Main ShowMenu="true" ShowPreMenu="false" SaveConfig="false" DefaultProfileName="multiplayer_test" ForceCacheLoadingAndSkipSaving="true" UpdateLogActive="false" />
<Screen Width="800" Height="600" Display="0" FullScreen="false" Vsync="false" />
<Graphics ShadowsActive="false" SSAOActive="false" WorldReflection="false" TextureQuality="1" />
<Engine LimitFPS="true" />
<Sound Volume="0" HRTF="false" />
<Physics PhysicsAccuracy="2" UpdatesPerSec="60" />
'@)
foreach($role in $roles) {
    $profileName="codex-multiplayer-smoke-$runId-$role"
    $profilePath=[IO.Path]::GetFullPath((Join-Path $profileParent $profileName))
    if(Test-Path -LiteralPath $profilePath) { throw "Refusing to reuse an existing test profile: $profilePath" }
    $profilePaths[$role]=$profilePath
    $config=$template -replace 'DefaultMainSettingsSDL2\s*=\s*"[^"]*"',('DefaultMainSettingsSDL2="'+$mainPath+'"')
    $config=$config -replace 'MainSaveFolder\s*=\s*"[^"]*"',('MainSaveFolder="'+$profileName+'"')
    [IO.File]::WriteAllText((Join-Path $run "$role-init.cfg"),$config)
}
$processes=@()
try {
    foreach($role in $roles) {
        $arguments=Join-TestProcessArguments @($role,(Join-Path $run "$role-init.cfg"),$run,[string]$Port)
        $processes += Start-Process -FilePath (Join-Path $context.Output 'smoke.exe') -ArgumentList $arguments -WorkingDirectory $retail -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run "$role-stdout.txt") -RedirectStandardError (Join-Path $run "$role-stderr.txt")
    }
    $timer=[Diagnostics.Stopwatch]::StartNew()
    while($timer.Elapsed.TotalSeconds -lt 200) {
        $running=@($processes | Where-Object { -not $_.HasExited })
        if($running.Count -eq 0) { break }
        $null=$running[0].WaitForExit(1000)
    }
    foreach($role in $roles) {
        Get-Content (Join-Path $run "$role-stdout.txt") -Tail 20
        Get-Content (Join-Path $run "$role-stderr.txt") -Tail 8
    }
    foreach($process in $processes) {
        if(-not $process.HasExited) { throw 'Full-game smoke exceeded its 200-second process limit.' }
        if($process.ExitCode -ne 0) { throw "Full-game smoke failed with exit code $($process.ExitCode)." }
    }
    foreach($role in $roles) {
        if(-not (Test-Path -LiteralPath (Join-Path $run "$role-passed.txt"))) { throw "Missing success marker for $role." }
    }
} finally {
    Stop-TestProcesses $processes
    foreach($role in $roles) {
        $path=$profilePaths[$role]
        if(-not (Test-Path -LiteralPath $path)) { continue }
        $item=Get-Item -LiteralPath $path
        $expected=[IO.Path]::GetFullPath((Join-Path $profileParent "codex-multiplayer-smoke-$runId-$role"))
        # Only these newly created, exact profile directories may be removed.
        if($item.FullName -ne $expected -or -not $expected.StartsWith($profileParent+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            Write-Warning "Profile cleanup refused unexpected path: $path"
            continue
        }
        $log=Join-Path $path 'hpl.log'
        if(Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination (Join-Path $run "$role-hpl.log") }
        if(-not $KeepProfiles) { Remove-Item -LiteralPath $expected -Recurse -Force }
    }
    Write-Output "Test artifacts: $run"
}
