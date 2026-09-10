param(
    [Parameter(Mandatory=$true)][string]$Package,
    [string]$GameDirectory='C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12'
)
$ErrorActionPreference='Stop'
$packageRoot=(Resolve-Path -LiteralPath $Package).Path
$gameRoot=(Resolve-Path -LiteralPath $GameDirectory).Path
if(-not (Test-Path -LiteralPath (Join-Path $gameRoot 'witcher3.exe'))){throw 'Pasta DX12 do The Witcher 3 não encontrada.'}
if(Get-Process -Name witcher3 -ErrorAction SilentlyContinue){throw 'Feche o The Witcher 3 antes de instalar.'}
$packageManifest=Get-Content -LiteralPath (Join-Path $packageRoot 'package.json') -Raw | ConvertFrom-Json
foreach($name in @('dxgi.dll','OptiScaler.ini')) {
    $file=Join-Path $packageRoot $name
    if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $packageManifest.hashes.$name){throw "Pacote alterado: $name"}
}
$existingDll=Join-Path $gameRoot 'dxgi.dll'
if((Get-Item -LiteralPath $existingDll).VersionInfo.ProductName -ne 'OptiScaler'){throw 'A dxgi.dll existente não foi identificada como OptiScaler; instalação interrompida.'}
$backupRoot=Join-Path $gameRoot ('TSR-backup-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backupRoot | Out-Null
$records=@()
foreach($name in @('dxgi.dll','OptiScaler.ini')) {
    $target=Join-Path $gameRoot $name
    Copy-Item -LiteralPath $target -Destination (Join-Path $backupRoot $name)
    $originalHash=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    if((Get-FileHash -LiteralPath (Join-Path $backupRoot $name) -Algorithm SHA256).Hash -ne $originalHash){throw 'Falha ao verificar backup.'}
    $records+=@{name=$name;originalHash=$originalHash;installedHash=$packageManifest.hashes.$name}
}
@{gameDirectory=$gameRoot;files=$records;created=(Get-Date).ToString('o')} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backupRoot 'restore.json') -Encoding UTF8
try {
    foreach($record in $records) {
        Copy-Item -LiteralPath (Join-Path $packageRoot $record.name) -Destination (Join-Path $gameRoot $record.name) -Force
        if((Get-FileHash -LiteralPath (Join-Path $gameRoot $record.name) -Algorithm SHA256).Hash -ne $record.installedHash){throw 'Falha ao verificar instalação.'}
    }
} catch {
    foreach($record in $records){Copy-Item -LiteralPath (Join-Path $backupRoot $record.name) -Destination (Join-Path $gameRoot $record.name) -Force}
    throw
}
Write-Output "Instalado. Backup verificado: $backupRoot"
