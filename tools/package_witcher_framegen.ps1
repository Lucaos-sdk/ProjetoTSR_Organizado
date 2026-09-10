param([string]$GameDirectory='C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$package=Join-Path $root 'artifacts/game-test/witcher3-framegen-2x'
New-Item -ItemType Directory -Path $package -Force | Out-Null
$intel=Join-Path $root '_OptiScaler_Source/external/xess'
foreach($name in @('libxess_fg.dll','libxell.dll')) {
 $source=Join-Path $intel "bin/$name"
 $signature=Get-AuthenticodeSignature -LiteralPath $source
 if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Intel'){throw "Intel signature not verified: $name"}
 Copy-Item -LiteralPath $source -Destination $package -Force
}
Copy-Item -LiteralPath (Join-Path $intel 'LICENSE.txt') -Destination (Join-Path $package 'Intel-LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $intel 'third-party-programs.txt') -Destination (Join-Path $package 'Intel-third-party-programs.txt') -Force
Copy-Item -LiteralPath (Join-Path $root '_OptiScaler_Source/x64/Release/a/OptiScaler.dll') -Destination (Join-Path $package 'dxgi.dll') -Force
$changes=@{
 'FrameGen.Enabled'='true';'FrameGen.FGInput'='upscaler';'FrameGen.FGOutput'='xefg';'FSRFG.AllowAsync'='false'
 'XeFG.InterpolationCount'='1';'XeFG.UIComposition'='false';'XeFG.IgnoreInitChecks'='false';'XeFG.DepthInverted'='true'
 'XeFG.HighResMV'='false';'XeFG.JitteredMV'='false';'XeFG.DebugView'='false'
 'OptiFG.DisableHUDFix'='true';'OptiFG.HUDFix'='false'
 'Libraries.XeFGPath'=(Join-Path $package 'libxess_fg.dll');'Libraries.XeLLPath'=(Join-Path $package 'libxell.dll')
 'Log.LogToFile'='true';'Log.LogLevel'='2';'Log.LogFileName'=(Join-Path $GameDirectory 'TSR_FrameGen.log')
}
$seen=@{};$section=''
$lines=foreach($line in Get-Content -LiteralPath (Join-Path $GameDirectory 'OptiScaler.ini')) {
 if($line -match '^\s*\[([^]]+)\]'){$section=$Matches[1]}
 if($line -match '^\s*([^;=]+?)\s*=') {
  $name=$Matches[1].Trim();$key="$section.$name"
  if($changes.ContainsKey($key)) {
   if($seen.ContainsKey($key)){throw "Duplicate key: $key"}
   $seen[$key]=$true;"$name = $($changes[$key])";continue
  }
 }
 $line
}
foreach($key in $changes.Keys){if(-not $seen.ContainsKey($key)){throw "Missing setting: $key"}}
[IO.File]::WriteAllLines((Join-Path $package 'OptiScaler.ini'),[string[]]$lines,[Text.UTF8Encoding]::new($false))
$hashes=@{}
foreach($name in @('dxgi.dll','OptiScaler.ini','libxess_fg.dll','libxell.dll')){$hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $package $name)).Hash}
@{kind='FSR 4.1.1 + XeFG 2x integration test';target=$GameDirectory;hashes=$hashes;changes=$changes;
  maxGeneratedFramesQueriedOnRX7600=1;multiFrameGeneration=$false;interpolationOwner='Intel';created=(Get-Date).ToString('o')} |
 ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $package 'package.json') -Encoding UTF8
Write-Output $package
