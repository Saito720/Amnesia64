Set-StrictMode -Version Latest

function Get-MultiplayerTestContext {
    param([ValidateSet('ui','game')][string]$Kind)
    $workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
    $output = Join-Path $workspace "bld/multiplayer-tests/$Kind"
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if(-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer / vswhere was not found.' }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if(-not $installation) { throw 'Install Visual Studio Desktop development with C++ first.' }
    $toolset = Get-ChildItem -LiteralPath (Join-Path $installation 'VC/Tools/MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    $sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
    $sdkVersion = Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'um/Windows.h') } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if(-not $toolset -or -not $sdkVersion) { throw 'A complete MSVC toolset and Windows SDK are required.' }
    [pscustomobject]@{
        Workspace=$workspace; Output=$output; Toolset=$toolset.FullName; Sdk=$sdk; SdkVersion=$sdkVersion.Name
        Compiler=(Join-Path $toolset.FullName 'bin/Hostx64/x64/cl.exe')
        MSBuild=(Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe')
    }
}

function Find-AmnesiaRetailDirectory {
    param([string]$RetailDirectory)
    $candidates = @()
    if($RetailDirectory) { $candidates += $RetailDirectory }
    else {
        $steamRoots = @()
        $steamRegistry = Get-ItemProperty -LiteralPath 'HKCU:/Software/Valve/Steam' -ErrorAction SilentlyContinue
        if($steamRegistry -and $steamRegistry.PSObject.Properties['SteamPath']) { $steamRoots += $steamRegistry.SteamPath }
        $steamRoots += Join-Path ${env:ProgramFiles(x86)} 'Steam'
        foreach($drive in Get-PSDrive -PSProvider FileSystem) { $steamRoots += Join-Path $drive.Root 'Steam' }
        foreach($steamRoot in @($steamRoots | Select-Object -Unique)) {
            $libraryFile = Join-Path $steamRoot 'steamapps/libraryfolders.vdf'
            if(Test-Path -LiteralPath $libraryFile) {
                foreach($match in [regex]::Matches([IO.File]::ReadAllText($libraryFile),'"path"\s+"([^"]+)"')) {
                    $steamRoots += $match.Groups[1].Value.Replace('\\','\')
                }
            }
        }
        foreach($steamRoot in @($steamRoots | Select-Object -Unique)) {
            $candidates += Join-Path $steamRoot 'steamapps/common/Amnesia The Dark Descent'
        }
    }
    foreach($candidate in $candidates) {
        if((Test-Path -LiteralPath (Join-Path $candidate 'config/main_init.cfg')) -and (Test-Path -LiteralPath (Join-Path $candidate 'resources.cfg'))) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw 'Retail assets were not found. Pass -RetailDirectory with the Amnesia installation directory.'
}

function Join-TestProcessArguments {
    param([string[]]$Values)
    # Windows CommandLineToArgvW quoting, including trailing backslashes.
    ($Values | ForEach-Object {
        $escaped = [regex]::Replace($_,'(\\*)"','$1$1\"')
        $escaped = [regex]::Replace($escaped,'(\\+)$','$1$1')
        '"'+$escaped+'"'
    }) -join ' '
}

function Stop-TestProcesses {
    param([object[]]$Processes)
    foreach($process in $Processes) {
        if(-not $process.HasExited) {
            Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
            $null = $process.WaitForExit(5000)
        }
    }
}
