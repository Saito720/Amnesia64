param([string]$RetailDirectory,[int]$Width=800,[int]$Height=600,[ValidateSet('Standalone','Steamworks')][string]$Backend='Standalone',[switch]$SkipBuild)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'TestSupport.ps1')
$context=Get-MultiplayerTestContext 'ui'
$retail=Find-AmnesiaRetailDirectory $RetailDirectory
if(-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -Kind ui -Backend $Backend }
$run=Join-Path $context.Output ([Guid]::NewGuid().ToString('N').Substring(0,12))
New-Item -ItemType Directory -Path $run | Out-Null
$process=$null
try {
    $arguments=Join-TestProcessArguments @([string]$Width,[string]$Height,$run)
    $process=Start-Process -FilePath (Join-Path $context.Output 'smoke.exe') -ArgumentList $arguments -WorkingDirectory $retail -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'stdout.txt') -RedirectStandardError (Join-Path $run 'stderr.txt')
    if(-not $process.WaitForExit(20000)) { throw 'UI smoke exceeded its 20-second limit.' }
    Get-Content (Join-Path $run 'stdout.txt')
    Get-Content (Join-Path $run 'stderr.txt')
    if($process.ExitCode -ne 0) { throw "UI smoke failed with exit code $($process.ExitCode)." }
} finally {
    if($process) { Stop-TestProcesses @($process) }
    Write-Output "Test artifacts: $run"
}
