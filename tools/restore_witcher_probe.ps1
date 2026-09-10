param([Parameter(Mandatory=$true)][string]$Backup)
$ErrorActionPreference='Stop'
if(Get-Process -Name witcher3 -ErrorAction SilentlyContinue){throw 'Feche o The Witcher 3 antes de restaurar.'}
$backupRoot=(Resolve-Path -LiteralPath $Backup).Path
$manifest=Get-Content -LiteralPath (Join-Path $backupRoot 'restore.json') -Raw | ConvertFrom-Json
$gameRoot=(Resolve-Path -LiteralPath $manifest.gameDirectory).Path
if((Split-Path -Parent $backupRoot) -ne $gameRoot -or -not (Test-Path -LiteralPath (Join-Path $gameRoot 'witcher3.exe'))){throw 'Backup não pertence à pasta do jogo.'}
foreach($record in $manifest.files) {
    if($record.name -notin @('dxgi.dll','OptiScaler.ini')){throw 'Arquivo inesperado no manifesto.'}
    if((Get-FileHash -LiteralPath (Join-Path $backupRoot $record.name) -Algorithm SHA256).Hash -ne $record.originalHash){throw 'Backup modificado.'}
    if((Get-FileHash -LiteralPath (Join-Path $gameRoot $record.name) -Algorithm SHA256).Hash -ne $record.installedHash){throw 'A instalação foi modificada depois do teste. Preserve essas mudanças antes de restaurar.'}
}
foreach($record in $manifest.files) {
    Copy-Item -LiteralPath (Join-Path $backupRoot $record.name) -Destination (Join-Path $gameRoot $record.name) -Force
    if((Get-FileHash -LiteralPath (Join-Path $gameRoot $record.name) -Algorithm SHA256).Hash -ne $record.originalHash){throw 'Falha na verificação da restauração.'}
}
Write-Output 'OptiScaler anterior restaurado e verificado. O backup foi preservado.'
