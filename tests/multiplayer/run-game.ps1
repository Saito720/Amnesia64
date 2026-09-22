param([string]$RetailDirectory,[int]$Port=27843,[ValidateSet('Standalone','Steamworks')][string]$Backend='Standalone',[switch]$SteamHostOnly,[switch]$SettingsOnly,[switch]$FontOnly,[string]$FontPath,[switch]$EnemiesOnly,[switch]$BackHallOnly,[switch]$QuitOnly,[switch]$QuitDirectly,[switch]$DoorBreakOnly,[ValidateSet('Auto','Bordered','Borderless')][string]$BorderMode='Auto',[switch]$SkipBuild,[switch]$KeepProfiles,[ValidateRange(320,8192)][int]$Width=800,[ValidateRange(240,8192)][int]$Height=600)
$ErrorActionPreference='Stop'
if(-not $PSBoundParameters.ContainsKey('BorderMode') -and -not $SettingsOnly) { $BorderMode='Bordered' }
. (Join-Path $PSScriptRoot 'TestSupport.ps1')
$context=Get-MultiplayerTestContext 'game'
$retail=Find-AmnesiaRetailDirectory $RetailDirectory
if($SteamHostOnly -and $Backend -ne 'Steamworks') { throw '-SteamHostOnly requires -Backend Steamworks and a signed-in account with access to the configured AppID.' }
if($SteamHostOnly -and $SettingsOnly) { throw '-SteamHostOnly and -SettingsOnly select different tests.' }
if($FontOnly -and ($SteamHostOnly -or $SettingsOnly -or $EnemiesOnly -or $BackHallOnly -or $QuitOnly -or $QuitDirectly -or $DoorBreakOnly)) { throw '-FontOnly cannot be combined with another focused test mode.' }
if($FontOnly) {
    if(-not $FontPath) { throw '-FontOnly requires -FontPath.' }
    $FontPath=[IO.Path]::GetFullPath($FontPath)
    if(-not (Test-Path -LiteralPath $FontPath -PathType Leaf)) { throw "Font file not found: $FontPath" }
}
if($EnemiesOnly -and ($SteamHostOnly -or $SettingsOnly)) { throw '-EnemiesOnly cannot be combined with another focused test mode.' }
if($BackHallOnly -and ($SteamHostOnly -or $SettingsOnly -or $EnemiesOnly)) { throw '-BackHallOnly cannot be combined with another focused test mode.' }
if(($QuitOnly -or $QuitDirectly) -and ($SteamHostOnly -or $SettingsOnly -or $EnemiesOnly -or $BackHallOnly)) { throw 'Quit tests cannot be combined with another focused test mode.' }
if($QuitOnly -and $QuitDirectly) { throw '-QuitOnly and -QuitDirectly select different affirmative exit paths.' }
if($DoorBreakOnly -and ($SteamHostOnly -or $SettingsOnly -or $EnemiesOnly -or $BackHallOnly -or $QuitOnly -or $QuitDirectly)) { throw '-DoorBreakOnly cannot be combined with another focused test mode.' }
$roles=if($FontOnly) { @('font') } elseif($SettingsOnly) { @('settings') } elseif($SteamHostOnly) { @('steam-host') } else { @('host','client') }
if(-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -Kind game -Backend $Backend }
if($Port -lt 1 -or $Port -gt 65535) { throw 'Port must be from 1 to 65535.' }
$runId=[Guid]::NewGuid().ToString('N').Substring(0,12)
$run=Join-Path $context.Output $runId
New-Item -ItemType Directory -Path $run | Out-Null
$enemyMap = $null
$enemyCache = $null
if($EnemiesOnly) {
    . (Join-Path $PSScriptRoot 'EnemyFixture.ps1')
    $enemyMap = New-MultiplayerEnemyFixture $retail $run
    $enemyHash=(Get-FileHash -LiteralPath $enemyMap -Algorithm SHA256).Hash.ToLowerInvariant()
    $enemyCacheRoot=[IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'HPL2/Amnesia/MultiplayerCache/objects'))
    $enemyCache=[IO.Path]::GetFullPath((Join-Path $enemyCacheRoot "$enemyHash.map"))
    if(Test-Path -LiteralPath $enemyCache) { throw 'A unique enemy fixture unexpectedly already exists in the cache.' }
}
$profileParent=[IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Amnesia'))
$profilePaths=@{}
$template=[IO.File]::ReadAllText((Join-Path $retail 'config/main_init.cfg'))
$mainPath=(Join-Path $run 'main-settings.cfg').Replace('\','/')
$borderSetting=switch($BorderMode) { 'Bordered' { ' Borderless="false"' }; 'Borderless' { ' Borderless="true"' }; default { '' } }
[IO.File]::WriteAllText($mainPath,@"
<Main ShowMenu="true" ShowPreMenu="false" SaveConfig="false" DefaultProfileName="multiplayer_test" ForceCacheLoadingAndSkipSaving="true" UpdateLogActive="false" />
<Screen Width="$Width" Height="$Height" Display="0" FullScreen="false" Vsync="false"$borderSetting />
<Graphics ShadowsActive="false" SSAOActive="false" WorldReflection="false" TextureQuality="1" />
<Engine LimitFPS="true" />
<Sound Volume="0" HRTF="false" />
<Physics PhysicsAccuracy="2" UpdatesPerSec="60" />
"@)
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
$savedEnemyMode=$env:CODEX_MP_ENEMIES
$savedEnemyMap=$env:CODEX_MP_ENEMY_MAP
$savedBackHallMode=$env:CODEX_MP_BACK_HALL
$savedQuitMode=$env:CODEX_MP_QUIT
$savedQuitDirectlyMode=$env:CODEX_MP_QUIT_DIRECTLY
$savedDoorBreakMode=$env:CODEX_MP_DOOR_BREAK
$savedFontTestPath=$env:CODEX_FONT_TEST_PATH
try {
    if($FontOnly) { $env:CODEX_FONT_TEST_PATH=$FontPath }
    if($EnemiesOnly) { $env:CODEX_MP_ENEMIES='1'; $env:CODEX_MP_ENEMY_MAP=$enemyMap }
    if($BackHallOnly) { $env:CODEX_MP_BACK_HALL='1' }
    if($DoorBreakOnly) { $env:CODEX_MP_DOOR_BREAK='1' }
    if($QuitOnly -or $QuitDirectly) { $env:CODEX_MP_QUIT='1' }
    if($QuitDirectly) { $env:CODEX_MP_QUIT_DIRECTLY='1' } elseif($QuitOnly) { Remove-Item Env:CODEX_MP_QUIT_DIRECTLY -ErrorAction SilentlyContinue }
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
    $env:CODEX_MP_ENEMIES=$savedEnemyMode
    $env:CODEX_MP_ENEMY_MAP=$savedEnemyMap
    $env:CODEX_MP_BACK_HALL=$savedBackHallMode
    $env:CODEX_MP_QUIT=$savedQuitMode
    $env:CODEX_MP_QUIT_DIRECTLY=$savedQuitDirectlyMode
    $env:CODEX_MP_DOOR_BREAK=$savedDoorBreakMode
    $env:CODEX_FONT_TEST_PATH=$savedFontTestPath
    Stop-TestProcesses $processes
    if($enemyCache -and (Test-Path -LiteralPath $enemyCache)) {
        $item=Get-Item -LiteralPath $enemyCache
        # The unique comment gives this run its own hash. Remove only this exact
        # ordinary file, including failed runs; existing downloaded maps remain.
        if($item.FullName -eq $enemyCache -and $item.DirectoryName -eq $enemyCacheRoot -and
           -not ($item.Attributes -band ([IO.FileAttributes]::ReparsePoint -bor [IO.FileAttributes]::Directory)) -and
           (Get-FileHash -LiteralPath $enemyCache -Algorithm SHA256).Hash.ToLowerInvariant() -eq $enemyHash) {
            Remove-Item -LiteralPath $enemyCache -Force
        } else { Write-Warning "Enemy cache cleanup refused unexpected file: $enemyCache" }
    }
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
