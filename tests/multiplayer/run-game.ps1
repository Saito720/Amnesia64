param([string]$RetailDirectory,[int]$Port=27843,[ValidateSet('Standalone','Steamworks')][string]$Backend='Standalone',[switch]$SteamHostOnly,[switch]$SettingsOnly,[switch]$FontOnly,[string]$FontPath,[switch]$EnemiesOnly,[switch]$BackHallOnly,[switch]$QuitOnly,[switch]$QuitDirectly,[switch]$DoorBreakOnly,[switch]$ScriptsOnly,[ValidateSet('Auto','Bordered','Borderless')][string]$BorderMode='Auto',[switch]$SkipBuild,[switch]$KeepProfiles,[ValidateRange(320,8192)][int]$Width=800,[ValidateRange(240,8192)][int]$Height=600)
$ErrorActionPreference='Stop'
if(-not $PSBoundParameters.ContainsKey('BorderMode') -and -not $SettingsOnly) { $BorderMode='Bordered' }
. (Join-Path $PSScriptRoot 'TestSupport.ps1')
$context=Get-MultiplayerTestContext 'game'
$retail=Find-AmnesiaRetailDirectory $RetailDirectory
if($SteamHostOnly -and $Backend -ne 'Steamworks') { throw '-SteamHostOnly requires -Backend Steamworks and a signed-in account with access to the configured AppID.' }
if($SteamHostOnly -and $SettingsOnly) { throw '-SteamHostOnly and -SettingsOnly select different tests.' }
if($ScriptsOnly -and ($SteamHostOnly -or $SettingsOnly -or $FontOnly -or $EnemiesOnly -or $BackHallOnly -or $QuitOnly -or $QuitDirectly -or $DoorBreakOnly)) { throw '-ScriptsOnly cannot be combined with another focused test mode.' }
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
$scriptMap = $null
$scriptCache = $null
if($ScriptsOnly) {
    $scriptMap = Join-Path $run 'codex_script_fixture.map'
    $source = [IO.File]::ReadAllText((Join-Path $retail 'maps/main/ch01/01_old_archives.map'))
    [IO.File]::WriteAllText($scriptMap,$source+"`n<!-- Isolated script regression $runId -->`n")
    [IO.File]::WriteAllText((Join-Path $run 'codex_script_fixture.scriptcfg'),"[Scripting]`nVersion=2`n")
    [IO.File]::WriteAllText((Join-Path $run 'codex_script_fixture.hps'),@'
void ServerOnStart() {
    SetPropStaticPhysics("tinderbox_1",true);
    float x=GetEntityPosX("tinderbox_1")+0.05f;
    SetEntityPos("tinderbox_1",x,GetEntityPosY("tinderbox_1"),GetEntityPosZ("tinderbox_1"));
    SetLocalVarInt("world_setpos",MathAbs(GetEntityPosX("tinderbox_1")-x)<0.001f ? 1 : 0);
    SetEntityCallbackFunc("tinderbox_1","WorldCallback");
    SetEntityPlayerInteractCallback("chair_wood_1","PlayerInteraction",false);
    AddTimer("unattributed",0.1f,"WorldTimer");
    AddTimer("unattributed-pending",30.0f,"WorldTimer");
}
void WorldTimer(string &in timer) { SetLocalVarInt("world_timer_actor",GetScriptPlayerId()); }
void WorldCallback(string &in entity,string &in type) {
    if(type=="CodexWorldProbe") SetLocalVarInt("world_actor",GetScriptPlayerId());
}
void PlayerInteraction(string &in entity) { SetLocalVarInt("interaction_actor",GetScriptPlayerId()); }
void ReplacementCollision(string &in parent,string &in child,int state) {
    RemovePlayerCollideCallback(child);
    AddPlayerCollideCallback(child,"ReplacementCollision",false,1);
    RemovePlayerCollideCallback(child);
    AddLocalVarInt("collision_replacement",1);
}
void OwnedTimer(string &in timer) { AddLocalVarInt("wrong_module_timer",1); }
void ServerOnEnter() {}
void ServerOnLeave() {}
void ServerOnUpdate(float step) {}
void ServerOnPlayerReady(int peer) {
    SetLanternDisabled(false);
    bool rosterReady=GetScriptPlayerCount()>=1 && GetScriptPlayerIdAt(0)==0;
    if(peer!=0) rosterReady=rosterReady && GetScriptPlayerCount()==2 && GetScriptPlayerIdAt(1)==peer;
    SetLocalVarInt("ready_context",GetScriptPlayerId()==peer && rosterReady ? 1 : 0);
    if(peer==0) AddLocalVarInt("ready_host",1);
    else AddLocalVarInt("ready_client",1);
    string owner=peer==0 ? "host" : "remote";
    bool empty=GetPlayerVarInt("number")==0 && GetPlayerVarFloat("fraction")==0 && GetPlayerVarString("text")=="";
    SetPlayerVarInt("number",peer==0 ? 11 : 22);
    SetPlayerVarFloat("fraction",peer==0 ? 0.125f : 0.25f);
    SetPlayerVarString("text",owner+" map");
    SetPlayerVarInt("number",peer==0 ? 111 : 122,true);
    SetPlayerVarFloat("fraction",peer==0 ? 1.125f : 1.25f,true);
    SetPlayerVarString("text",owner+" campaign",true);
    string@ copy=GetPlayerVarString("text");
    copy+=" modified copy";
    bool values=CheckPlayerValues(peer);
    SetLocalVarInt(peer==0 ? "vars_ready_host" : "vars_ready_client",empty && values ? 1 : 0);
    PublishPlayerVar("owner",owner+" \"quoted\"\nvalue");
    PublishPlayerVar("stage","ready");
}
bool CheckPlayerValues(int peer) {
    string owner=peer==0 ? "host" : "remote";
    return GetPlayerVarInt("number")== (peer==0 ? 11 : 22) &&
        GetPlayerVarFloat("fraction")== (peer==0 ? 0.125f : 0.25f) &&
        GetPlayerVarString("text")==owner+" map" &&
        GetPlayerVarInt("number",true)== (peer==0 ? 111 : 122) &&
        GetPlayerVarFloat("fraction",true)== (peer==0 ? 1.125f : 1.25f) &&
        GetPlayerVarString("text",true)==owner+" campaign";
}
void TargetPeer(int peer) {
    if(!SelectScriptPlayer(peer)) return;
    SetLocalVarInt("vars_target",CheckPlayerValues(peer) ? 1 : 0);
    PublishPlayerVar("stage","targeted");
    float x=GetPlayerPosX()+0.05f;
    float y=GetPlayerPosY();
    float z=GetPlayerPosZ();
    SetPlayerPos(x,y,z);
    SetLocalVarInt("position_immediate", MathAbs(GetPlayerPosX()-x)<0.001f && MathAbs(GetPlayerPosY()-y)<0.001f && MathAbs(GetPlayerPosZ()-z)<0.001f ? 1 : 0);
    SetPlayerHealth(peer==0 ? 63 : 73);
    RunClientCallback("Personal", "quoted\"literal");
}
void BeginCompletion(int peer) {
    if(!SelectScriptPlayer(peer)) return;
    SetLanternActive(false,false);
    SetLanternLitCallback("LanternDone");
}
void CheckAfterRollback(int peer) {
    if(!SelectScriptPlayer(peer)) return;
    SetLocalVarInt("vars_rollback",CheckPlayerValues(peer) ? 1 : 0);
}
void LanternDone(bool active) {
    if(!active) return;
    AddLocalVarInt("completion_count",1);
    SetLocalVarInt("completion_player",GetScriptPlayerId());
    SetPlayerHealth(GetScriptPlayerId()==0 ? 61 : 71);
}
'@)
    [IO.File]::WriteAllText((Join-Path $run 'global.hps'),@'
int initializerActor=-2;
class GlobalInitializer {
    GlobalInitializer() {
        initializerActor=GetScriptPlayerId();
        if(SelectScriptPlayer(0)) {
            SetPlayerVarInt("initializer",23,true);
            AddTimer("initializer-owner",0.1f,"InitializerTimer");
        }
    }
}
GlobalInitializer initializer;
void InitializerTimer(string &in timer) {
    SetLocalVarInt("global_initializer_owner",initializerActor==-1 && GetScriptPlayerId()==0 &&
        GetPlayerVarInt("initializer",true)==23 ? 1 : -1);
}
void ServerOnGameStart() {}
void ServerOnPlayerReady(int peer) {
    SetPlayerVarInt("timer_actor",peer+1);
    SetPlayerVarString("timer_module","global");
    AddTimer("owned",0.1f,"OwnedTimer");
}
void OwnedTimer(string &in timer) {
    int peer=GetScriptPlayerId();
    if(peer<0 || GetPlayerVarInt("timer_actor")!=peer+1 || GetPlayerVarString("timer_module")!="global") {
        SetLocalVarInt("timer_owner_error",1); return;
    }
    AddLocalVarInt(peer==0 ? "global_timer_host" : "global_timer_client",1);
}
'@)
    [IO.File]::WriteAllText((Join-Path $run 'inventory.hps'),@'
void ServerOnGameStart() {}
void ServerOnPlayerReady(int peer) {
    SetPlayerVarInt("timer_actor",peer+1);
    SetPlayerVarString("timer_module","inventory");
    AddTimer("owned",0.1f,"OwnedTimer");
}
void OwnedTimer(string &in timer) {
    int peer=GetScriptPlayerId();
    if(peer<0 || GetPlayerVarInt("timer_actor")!=peer+1 || GetPlayerVarString("timer_module")!="inventory") {
        SetLocalVarInt("timer_owner_error",1); return;
    }
    AddLocalVarInt(peer==0 ? "inventory_timer_host" : "inventory_timer_client",1);
}
'@)
    [IO.File]::WriteAllText((Join-Path $run 'codex_script_fixture.client.hps'),@'
int personalEvents=0;
float elapsed=0;
bool publicationMismatch=false;
int starts=0;
int enters=0;
float rollbackBaseline=0;
int rollbackTimerFires=0;
void ClientOnStart() {
    starts++;
    FadePlayerFOVMulTo(0.95f,100);
    AddTimer("boot",0.2f,"BootTimer");
}
void ClientOnEnter() { enters++; }
void ClientOnLeave() {}
void ClientOnUpdate(float step) {
    elapsed+=step;
    string owner=GetScriptPlayerId()==0 ? "host" : "remote";
    string value=GetPublishedScriptVar("owner");
    if(value!="" && value!=owner+" \"quoted\"\nvalue") publicationMismatch=true;
}
void InspectPublication(string &in stage) {
    string owner=GetScriptPlayerId()==0 ? "host" : "remote";
    string value=GetPublishedScriptVar("owner");
    if(value=="") { FadePlayerRollTo(0,100,100); return; }
    string@ copy=GetPublishedScriptVar("owner");
    copy+=" modified copy";
    bool intact=GetPublishedScriptVar("owner")==owner+" \"quoted\"\nvalue";
    if(publicationMismatch || !intact) { FadePlayerRollTo(-23,100,100); return; }
    FadePlayerRollTo(GetPublishedScriptVar("stage")==stage ? 23 : 0,100,100);
}
void BootTimer(string &in timer) { FadePlayerAspectMulTo(0.91f,100); }
void Personal(string &in argument) {
    if(argument!="quoted\"literal") return;
    personalEvents++;
    float goal=0.6f+personalEvents*0.1f;
    if(GetScriptPlayerId()==0) goal+=0.1f;
    FadePlayerFOVMulTo(goal,100);
    AddTimer("personal",0.2f,"PersonalTimer");
}
void PersonalTimer(string &in timer) { FadePlayerAspectMulTo(GetScriptPlayerId()==0 ? 0.71f : 0.81f,100); }
void Reveal(string &in unused) { FadePlayerRollTo(elapsed,100,100); }
void ArmRollback(string &in unused) {
    rollbackBaseline=elapsed;
    AddTimer("rollback",3.0f,"RollbackTimer");
}
void RollbackTimer(string &in timer) { rollbackTimerFires++; }
void VerifyRollback(string &in unused) {
    string owner=GetScriptPlayerId()==0 ? "host" : "remote";
    if(starts!=1 || enters!=1 || personalEvents!=1 || publicationMismatch || rollbackTimerFires>1 ||
       GetPublishedScriptVar("owner")!=owner+" \"quoted\"\nvalue" || GetPublishedScriptVar("stage")!="targeted") {
        FadePlayerRollTo(-31,100,100); return;
    }
    FadePlayerRollTo(rollbackTimerFires==1 && elapsed>rollbackBaseline ? 31 : 0,100,100);
}
'@)
    [IO.File]::WriteAllText((Join-Path $run 'codex_script_broken.map'),$source+"`n<!-- Invalid authority companion regression $runId -->`n")
    [IO.File]::WriteAllText((Join-Path $run 'codex_script_broken.scriptcfg'),"[Scripting]`nVersion=2`n")
    [IO.File]::WriteAllText((Join-Path $run 'codex_script_broken.client.hps'),"void ClientOnStart() { FadePlayerFOVMulTo(0.4f,100); }")
    [IO.File]::WriteAllText((Join-Path $run 'codex_script_broken.hps'),"void ServerOnStart(float wrong) {}")
    $scriptHash=(Get-FileHash -LiteralPath $scriptMap -Algorithm SHA256).Hash.ToLowerInvariant()
    $scriptCacheRoot=[IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'HPL2/Amnesia/MultiplayerCache/objects'))
    $scriptCache=[IO.Path]::GetFullPath((Join-Path $scriptCacheRoot "$scriptHash.map"))
    if(Test-Path -LiteralPath $scriptCache) { throw 'A unique script fixture unexpectedly already exists in the cache.' }
}
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
$savedScriptsMode=$env:CODEX_MP_SCRIPTS
$savedScriptMap=$env:CODEX_MP_SCRIPT_MAP
try {
    if($ScriptsOnly) { $env:CODEX_MP_SCRIPTS='1'; $env:CODEX_MP_SCRIPT_MAP=$scriptMap }
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
    $env:CODEX_MP_SCRIPTS=$savedScriptsMode
    $env:CODEX_MP_SCRIPT_MAP=$savedScriptMap
    Stop-TestProcesses $processes
    if($scriptCache -and (Test-Path -LiteralPath $scriptCache)) {
        $item=Get-Item -LiteralPath $scriptCache
        if($item.FullName -eq $scriptCache -and $item.DirectoryName -eq $scriptCacheRoot -and
           -not ($item.Attributes -band ([IO.FileAttributes]::ReparsePoint -bor [IO.FileAttributes]::Directory)) -and
           (Get-FileHash -LiteralPath $scriptCache -Algorithm SHA256).Hash.ToLowerInvariant() -eq $scriptHash) {
            Remove-Item -LiteralPath $scriptCache -Force
        } else { Write-Warning "Script cache cleanup refused unexpected file: $scriptCache" }
    }
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
