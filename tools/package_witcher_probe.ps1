param([string]$GameDirectory='C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$packageRoot=Join-Path $projectRoot 'artifacts/game-test/witcher3-tsr-probe'
New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
$changes=@{
    'Upscalers.Dx12Upscaler'='tsr_probe'
    'FrameGen.Enabled'='false'
    'FrameGen.FGInput'='nofg'
    'FrameGen.FGOutput'='nofg'
    'Log.LogToFile'='true'
    'Log.LogLevel'='2'
    'Log.LogFileName'=(Join-Path $GameDirectory 'TSR_Probe.log')
}
$seen=@{};$section=''
$lines=foreach($line in Get-Content -LiteralPath (Join-Path $GameDirectory 'OptiScaler.ini')) {
    if($line -match '^\s*\[([^]]+)\]'){$section=$Matches[1]}
    if($line -match '^\s*([^;=]+?)\s*=') {
        $key=$Matches[1].Trim();$qualified="$section.$key"
        if($changes.ContainsKey($qualified)) {
            if($seen.ContainsKey($qualified)){throw "Chave duplicada: $qualified"}
            $seen[$qualified]=$true
            "$key = $($changes[$qualified])"
            continue
        }
    }
    $line
}
foreach($key in $changes.Keys){if(-not $seen.ContainsKey($key)){throw "Chave ausente: $key"}}
[IO.File]::WriteAllLines((Join-Path $packageRoot 'OptiScaler.ini'),[string[]]$lines,[Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $projectRoot '_OptiScaler_Source/x64/Release/a/OptiScaler.dll') -Destination (Join-Path $packageRoot 'dxgi.dll') -Force
$hashes=@{}
foreach($name in @('dxgi.dll','OptiScaler.ini')){$hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $packageRoot $name) -Algorithm SHA256).Hash}
@{kind='TSR compatibility probe';renderer='FSR 2.1.2';customTemporal=$false;neural=$false;target=$GameDirectory;hashes=$hashes;created=(Get-Date).ToString('o')} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $packageRoot 'package.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/WITCHER3_PROBE.md') -Destination (Join-Path $packageRoot 'LEIA-ME.md') -Force
Write-Output $packageRoot
