param([ValidateSet('XeSS','FSR2')][string]$InputMode='XeSS')
$ErrorActionPreference='Stop'
if(Get-Process -Name witcher3 -ErrorAction SilentlyContinue){throw 'Feche o jogo antes de ajustar a configuração.'}
$settings=Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'The Witcher 3/dx12user.settings'
$project=Split-Path -Parent $PSScriptRoot
$backup=Join-Path $project ('artifacts/game-test/witcher3-settings-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backup | Out-Null
Copy-Item -LiteralPath $settings -Destination (Join-Path $backup 'dx12user.settings.original')
$originalHash=(Get-FileHash -LiteralPath $settings).Hash
if((Get-FileHash -LiteralPath (Join-Path $backup 'dx12user.settings.original')).Hash -ne $originalHash){throw 'Falha na verificação do backup.'}
$changes=@{
    'Viewport.Resolution'='"1920x1080"'
    'Viewport.ReflexMode'='0'
    'PostProcess.AAMode'=$(if($InputMode -eq 'XeSS'){'7'}else{'5'})
    'PostProcess.XESSQuality'='2'
    'PostProcess.FSR2Quality'='1'
    'PostProcess.EnableFSR1'='false'
    'PostProcess.DLSSGMode'='0'
    'Rendering.AllowStreamline'='false'
    'Rendering.AllowDLSS'='false'
    'Rendering.AllowDLSSG'='false'
    'Rendering.AllowReflex'='false'
    'Rendering.DynamicResolutionPercentage'='-1'
}
$seen=@{};$section='';$record=@()
$lines=foreach($line in Get-Content -LiteralPath $settings) {
    if($line -match '^\s*\[([^]]+)\]'){$section=$Matches[1]}
    if($line -match '^\s*([^;=]+?)\s*=(.*)$') {
        $key=$Matches[1].Trim();$old=$Matches[2];$qualified="$section.$key"
        if($changes.ContainsKey($qualified)) {
            if($seen.ContainsKey($qualified)){throw "Chave duplicada: $qualified"}
            $seen[$qualified]=$true
            if($old -ne $changes[$qualified]){$record+=@{setting=$qualified;before=$old;after=$changes[$qualified]}}
            "$key=$($changes[$qualified])"
            continue
        }
    }
    $line
}
foreach($key in $changes.Keys){if(-not $seen.ContainsKey($key)){throw "Chave ausente: $key"}}
$prepared=Join-Path $backup 'dx12user.settings.prepared'
[IO.File]::WriteAllLines($prepared,[string[]]$lines,[Text.UTF8Encoding]::new($false))
if((Get-FileHash -LiteralPath $settings).Hash -ne $originalHash){throw 'Configuração alterada por outro processo; operação cancelada.'}
Copy-Item -LiteralPath $prepared -Destination $settings -Force
if((Get-FileHash -LiteralPath $settings).Hash -ne (Get-FileHash -LiteralPath $prepared).Hash){throw 'Falha na verificação da gravação.'}
@{file=$settings;backup=$backup;originalHash=$originalHash;installedHash=(Get-FileHash -LiteralPath $settings).Hash;changes=$record} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backup 'changes.json') -Encoding UTF8
$record | ConvertTo-Json -Compress
"Configuração verificada. Backup: $backup"
