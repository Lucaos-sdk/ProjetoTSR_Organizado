param(
    [ValidateSet('Prepare','Apply','Restore')][string]$Mode='Prepare',
    [string]$GameDirectory='C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12'
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$profile=Join-Path $root 'artifacts/game-test/witcher3-visual-01'
$ini=Join-Path $GameDirectory 'OptiScaler.ini'
$manifestPath=Join-Path $profile 'profile.json'
function Read-Values([string[]]$Lines) {
    $values=@{};$section=''
    foreach($line in $Lines) {
        if($line -match '^\s*\[([^]]+)\]'){$section=$Matches[1]}
        if($line -match '^\s*([^;=]+?)\s*=\s*(.*)$') {
            $key="$section.$($Matches[1].Trim())"
            if($values.ContainsKey($key)){throw "Duplicate setting: $key"}
            $values[$key]=$Matches[2]
        }
    }
    return $values
}
function Replace-Values([string[]]$Lines,[hashtable]$Changes) {
    $section='';$seen=@{}
    $result=foreach($line in $Lines) {
        if($line -match '^\s*\[([^]]+)\]'){$section=$Matches[1]}
        if($line -match '^\s*([^;=]+?)\s*=') {
            $name=$Matches[1].Trim();$key="$section.$name"
            if($Changes.ContainsKey($key)){$seen[$key]=$true;"$name = $($Changes[$key])";continue}
        }
        $line
    }
    foreach($key in $Changes.Keys){if(-not $seen.ContainsKey($key)){throw "Missing setting: $key"}}
    return $result
}
if(-not (Test-Path -LiteralPath (Join-Path $GameDirectory 'witcher3.exe'))){throw 'Game executable missing.'}
$dll=Join-Path $GameDirectory 'dxgi.dll'
if((Get-Item -LiteralPath $dll).VersionInfo.ProductName -notmatch 'OptiScaler'){throw 'Expected installed OptiScaler.'}
if($Mode -eq 'Prepare') {
    if(Test-Path -LiteralPath $manifestPath){throw 'Profile already prepared; preserve its original backup.'}
    New-Item -ItemType Directory -Path $profile -Force | Out-Null
    $originalHash=(Get-FileHash -LiteralPath $ini).Hash
    $lines=Get-Content -LiteralPath $ini;$values=Read-Values $lines
    if($values['Upscalers.Dx12Upscaler'].Trim() -ne 'tsr_probe'){throw 'Expected the validated diagnostic renderer.'}
    $changes=@{
        'Sharpness.Shader'='rcas';'Sharpness.OverrideSharpness'='true';'Sharpness.Sharpness'='0.25'
        'CAS.Enabled'='true';'CAS.MotionSharpnessEnabled'='false';'CAS.ContrastEnabled'='false';'CAS.SharpenerDebug'='false'
    }
    $prepared=Replace-Values $lines $changes
    $before=@{};foreach($key in $changes.Keys){$before[$key]=$values[$key]}
    Copy-Item -LiteralPath $ini -Destination (Join-Path $profile 'OptiScaler.before.ini')
    if((Get-FileHash -LiteralPath (Join-Path $profile 'OptiScaler.before.ini')).Hash -ne $originalHash){throw 'Config changed during preparation.'}
    [IO.File]::WriteAllLines((Join-Path $profile 'OptiScaler.visual.ini'),[string[]]$prepared,[Text.UTF8Encoding]::new($false))
    @{gameDirectory=$GameDirectory;originalHash=$originalHash;dllHash=(Get-FileHash -LiteralPath $dll).Hash;
      preparedHash=(Get-FileHash -LiteralPath (Join-Path $profile 'OptiScaler.visual.ini')).Hash;
      before=$before;after=$changes;description='Existing OptiScaler RCAS, strength 0.25; no neural renderer'} |
      ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    Write-Output 'Visual profile prepared; game not changed.'
    exit
}
if(Get-Process -Name witcher3 -ErrorAction SilentlyContinue){throw 'Feche o The Witcher 3 antes de aplicar ou restaurar.'}
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if((Resolve-Path -LiteralPath $GameDirectory).Path -ne (Resolve-Path -LiteralPath $manifest.gameDirectory).Path){throw 'Profile belongs to another game directory.'}
if((Get-FileHash -LiteralPath $dll).Hash -ne $manifest.dllHash){throw 'OptiScaler DLL changed since preparation.'}
$currentHash=(Get-FileHash -LiteralPath $ini).Hash
$lines=Get-Content -LiteralPath $ini;$values=Read-Values $lines;$desired=@{}
if($Mode -eq 'Apply') {
    if((Get-FileHash -LiteralPath (Join-Path $profile 'OptiScaler.visual.ini')).Hash -ne $manifest.preparedHash){throw 'Prepared profile changed.'}
    foreach($p in $manifest.after.PSObject.Properties){$desired[$p.Name]=[string]$p.Value}
    $expected=$manifest.before
} else {
    foreach($p in $manifest.before.PSObject.Properties){$desired[$p.Name]=[string]$p.Value}
    $expected=$manifest.after
}
foreach($key in $desired.Keys) {
    if(-not $values.ContainsKey($key)){throw "Missing setting: $key"}
    if($values[$key].Trim() -ne ([string]$expected.$key).Trim() -and $values[$key].Trim() -ne $desired[$key].Trim()) {
        throw "A configuracao $key foi alterada manualmente. Preserve essa escolha antes de trocar o perfil."
    }
}
$updated=Replace-Values $lines $desired
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$backup=Join-Path $profile "OptiScaler.$stamp.ini"
Copy-Item -LiteralPath $ini -Destination $backup
if((Get-FileHash -LiteralPath $backup).Hash -ne $currentHash -or (Get-FileHash -LiteralPath $ini).Hash -ne $currentHash){throw 'Config changed before update.'}
try {
    [IO.File]::WriteAllLines($ini,[string[]]$updated,[Text.UTF8Encoding]::new($false))
    $actual=Read-Values (Get-Content -LiteralPath $ini)
    foreach($key in $desired.Keys){if($actual[$key] -ne $desired[$key]){throw "Setting verification failed: $key"}}
} catch {Copy-Item -LiteralPath $backup -Destination $ini -Force;throw}
@{mode=$Mode;time=(Get-Date).ToString('o');beforeHash=$currentHash;afterHash=(Get-FileHash -LiteralPath $ini).Hash;backup=$backup} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $profile "operation-$stamp.json") -Encoding UTF8
Write-Output "Perfil $Mode aplicado e verificado. Backup preservado."
