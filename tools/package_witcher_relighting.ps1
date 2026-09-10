param([string]$GameDirectory='C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12',
      [ValidatePattern('^[a-z0-9-]+$')][string]$PackageName='witcher3-graphical-relighting',
      [ValidateSet('fixture_v1','world_v2')][string]$Model='world_v2',
      [switch]$KeepExistingSettings)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$packageRoot=Join-Path $projectRoot ('artifacts/game-test/'+$PackageName)
if(Test-Path -LiteralPath $packageRoot){throw 'Pacote já existe; preserve a versão anterior antes de recriar.'}
$sourceDll=Join-Path $projectRoot '_OptiScaler_Source/x64/Release/a/OptiScaler.dll'
$sourceIni=Join-Path $GameDirectory 'OptiScaler.ini'
$iniBytes=[IO.File]::ReadAllBytes($sourceIni)
$iniText=[Text.Encoding]::UTF8.GetString($iniBytes)
if(!$KeepExistingSettings -and $iniText -match '(?im)^\s*\[TSRRelighting\]'){throw 'Seção TSRRelighting já existe; usar KeepExistingSettings para preservar.'}
if($KeepExistingSettings -and $iniText -notmatch '(?im)^\s*\[TSRRelighting\]'){throw 'Configuração TSR anterior não encontrada.'}
New-Item -ItemType Directory -Path $packageRoot | Out-Null
Copy-Item -LiteralPath $sourceDll -Destination (Join-Path $packageRoot 'dxgi.dll')
# Preserve every original byte, including personal settings and paths to AMD/Intel libraries.
$suffix=[Text.Encoding]::UTF8.GetBytes("`r`n[TSRRelighting]`r`nEnabled=1`r`nStrengthPercent=35`r`n")
[IO.File]::WriteAllBytes((Join-Path $packageRoot 'OptiScaler.ini'),[byte[]]($iniBytes+$suffix))
if($KeepExistingSettings){[IO.File]::WriteAllBytes((Join-Path $packageRoot 'OptiScaler.ini'),$iniBytes)}
$hashes=@{}
foreach($name in @('dxgi.dll','OptiScaler.ini')){$hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $packageRoot $name) -Algorithm SHA256).Hash}
$strength=0.35
if($KeepExistingSettings){
    $section=[regex]::Match($iniText,'(?ms)^\s*\[TSRRelighting\]\s*\r?\n(?<body>.*?)(?=^\s*\[|\z)').Groups['body'].Value
    $value=[regex]::Match($section,'(?im)^\s*StrengthPercent\s*=\s*(\d+)')
    if($value.Success){$strength=[Math]::Min([double]$value.Groups[1].Value,100)/100}
}
$manifest=@{name=$PackageName;created=(Get-Date).ToString('o');hashes=$hashes;settingsPreserved=[bool]$KeepExistingSettings;
    originalGameHashes=@{'dxgi.dll'=(Get-FileHash -LiteralPath (Join-Path $GameDirectory 'dxgi.dll') -Algorithm SHA256).Hash;
                        'OptiScaler.ini'=(Get-FileHash -LiteralPath $sourceIni -Algorithm SHA256).Hash};
    ownModel=$Model;cameraAssociation='unique_jitter_experimental';strength=$strength;hotkey='F8';gameValidation='pending';
    worldAnchored=($Model -eq 'world_v2');smoothingDefault=$(if($Model -eq 'world_v2'){1}else{0})}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $packageRoot 'package.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/GAME_RELIGHTING.md') -Destination (Join-Path $packageRoot 'LEIA-ME.md')
Write-Output $packageRoot
