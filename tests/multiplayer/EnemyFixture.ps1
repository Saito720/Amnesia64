# Build a disposable authored map from unchanged retail content. The three
# inactive retail enemies and an isolated navigation grid allow late-join
# baselines to use the same native loader as campaign maps.
function New-MultiplayerEnemyFixture([string]$Retail, [string]$Output) {
    [xml]$map = [IO.File]::ReadAllText((Join-Path $Retail 'maps/main/ch01/01_old_archives.map'))
    $null = $map.DocumentElement.AppendChild($map.CreateComment('Isolated enemy regression '+[IO.Path]::GetFileName($Output)))
    $contents = $map.Level.MapData.MapContents
    if(-not $contents -or -not $contents.Entities -or -not $contents.FileIndex_Entities) {
        throw 'Old Archives does not have the expected map XML structure.'
    }
    $nextId = 1 + [int](($map.SelectNodes('//*[@ID]') | ForEach-Object { [int]$_.GetAttribute('ID') } | Measure-Object -Maximum).Maximum)
    $files = @(
        @('codex_enemy_grunt', 'entities/enemy/servant_grunt/servant_grunt.ent', '0 40 0'),
        @('codex_enemy_brute', 'entities/enemy/servant_brute/servant_brute.ent', '20 40 0'),
        @('codex_enemy_water', 'entities/enemy/waterlurker/waterlurker.ent', '-20 40 0')
    )
    $fileId = [int]$contents.FileIndex_Entities.NumOfFiles
    foreach($fixture in $files) {
        if(-not (Test-Path -LiteralPath (Join-Path $Retail $fixture[1]))) { throw "Enemy fixture asset is missing: $($fixture[1])" }
        $file = $map.CreateElement('File')
        $file.SetAttribute('Id', [string]$fileId); $file.SetAttribute('Path', $fixture[1])
        $null = $contents.FileIndex_Entities.AppendChild($file)
        $entity = $map.CreateElement('Entity')
        foreach($entry in @{ Active='false'; FileIndex=[string]$fileId; Group='0'; ID=[string]$nextId; Name=$fixture[0]; Rotation='0 0 0'; Scale='1 1 1'; Tag=''; WorldPos=$fixture[2] }.GetEnumerator()) {
            $entity.SetAttribute($entry.Key, $entry.Value)
        }
        $variables = $map.CreateElement('UserVariables')
        foreach($entry in @{ DisableTriggers='false'; CallbackFunc=''; Hallucination='false' }.GetEnumerator()) {
            $variable = $map.CreateElement('Var'); $variable.SetAttribute('Name', $entry.Key); $variable.SetAttribute('Value', $entry.Value)
            $null = $variables.AppendChild($variable)
        }
        $null = $entity.AppendChild($variables); $null = $contents.Entities.AppendChild($entity)
        ++$fileId; ++$nextId
    }
    $contents.FileIndex_Entities.SetAttribute('NumOfFiles', [string]$fileId)
    for($x=-24; $x -le 24; $x+=4) {
        for($z=-16; $z -le 8; $z+=4) {
            $node = $map.CreateElement('Area')
            foreach($entry in @{ Active='true'; AreaType='PathNode'; Group='0'; ID=[string]$nextId; Mesh=''; Name="CodexEnemyNode_$nextId"; Rotation='0 0 0'; Scale='1 1 1'; Tag=''; WorldPos="$x 40 $z" }.GetEnumerator()) {
                $node.SetAttribute($entry.Key, $entry.Value)
            }
            $null = $node.AppendChild($map.CreateElement('UserVariables'))
            $null = $contents.Entities.AppendChild($node); ++$nextId
        }
    }
    $path = Join-Path $Output 'codex_enemy_fixture.map'
    $map.Save($path)
    return $path
}
