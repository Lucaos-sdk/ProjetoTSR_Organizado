$ErrorActionPreference='Stop'
$root=Join-Path (Split-Path -Parent $PSScriptRoot) ('artifacts/probe-install-test-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$game=Join-Path $root 'fake game with spaces'
$package=Join-Path $root 'package'
New-Item -ItemType Directory -Path $game,$package | Out-Null
$reference=Join-Path (Split-Path -Parent $PSScriptRoot) '_OptiScaler_Source/x64/Release/a/OptiScaler.dll'
Copy-Item -LiteralPath $reference -Destination (Join-Path $game 'dxgi.dll')
Copy-Item -LiteralPath $reference -Destination (Join-Path $package 'dxgi.dll')
Set-Content -LiteralPath (Join-Path $game 'witcher3.exe') -Value 'fixture only, never executed'
Set-Content -LiteralPath (Join-Path $game 'OptiScaler.ini') -Value '[Original]'
Set-Content -LiteralPath (Join-Path $package 'OptiScaler.ini') -Value '[Probe]'
$originalHash=(Get-FileHash -LiteralPath (Join-Path $game 'OptiScaler.ini')).Hash
$hashes=@{}
foreach($name in @('dxgi.dll','OptiScaler.ini')){$hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $package $name)).Hash}
@{hashes=$hashes} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'package.json')
& (Join-Path $PSScriptRoot 'install_witcher_probe.ps1') -Package $package -GameDirectory $game
$backup=(Get-ChildItem -LiteralPath $game -Directory | Where-Object Name -Like 'TSR-backup-*').FullName
Set-Content -LiteralPath (Join-Path $game 'OptiScaler.ini') -Value '[User changed it]'
$rejected=$false
try { & (Join-Path $PSScriptRoot 'restore_witcher_probe.ps1') -Backup $backup } catch {$rejected=$true}
if(-not $rejected){throw 'Restore overwrote a later user change.'}
Copy-Item -LiteralPath (Join-Path $package 'OptiScaler.ini') -Destination (Join-Path $game 'OptiScaler.ini') -Force
& (Join-Path $PSScriptRoot 'restore_witcher_probe.ps1') -Backup $backup
if((Get-FileHash -LiteralPath (Join-Path $game 'OptiScaler.ini')).Hash -ne $originalHash){throw 'Original config was not restored.'}
Add-Content -LiteralPath (Join-Path $package 'OptiScaler.ini') -Value 'corrupt package'
$rejected=$false
try { & (Join-Path $PSScriptRoot 'install_witcher_probe.ps1') -Package $package -GameDirectory $game } catch {$rejected=$true}
if(-not $rejected){throw 'Modified package was installed.'}
if((Get-FileHash -LiteralPath (Join-Path $game 'OptiScaler.ini')).Hash -ne $originalHash){throw 'Failed installation modified original config.'}
'PASS install, verified backup, restore, paths with spaces, modified-package refusal and protection of subsequent edits'
