param([string]$GameDirectory='C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$package=Join-Path $root 'artifacts/game-test/witcher3-fsr411'
New-Item -ItemType Directory -Path $package -Force | Out-Null
$sdk=Join-Path $root '_OptiScaler_Source/external/FidelityFX-SDK/Kits/FidelityFX'
$source=Join-Path $sdk 'signedbin/amd_fidelityfx_upscaler_dx12.dll'
$sig=Get-AuthenticodeSignature -LiteralPath $source
if($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'Advanced Micro Devices'){throw 'AMD signature could not be verified.'}
Copy-Item -LiteralPath $source -Destination $package -Force
Copy-Item -LiteralPath (Join-Path $sdk 'docs/license.md') -Destination (Join-Path $package 'AMD-LICENSE.md') -Force
Copy-Item -LiteralPath (Join-Path $root '_OptiScaler_Source/x64/Release/a/OptiScaler.dll') -Destination (Join-Path $package 'dxgi.dll') -Force
$changes=@{
 'Upscalers.Dx12Upscaler'='ffx';'FSR.UpscalerIndex'='0';'FSR.Fsr4ForceModel'='auto'
 'Libraries.FfxDx12SRPath'=(Join-Path $package 'amd_fidelityfx_upscaler_dx12.dll')
 'FrameGen.Enabled'='false';'FrameGen.FGInput'='nofg';'FrameGen.FGOutput'='nofg'
 'Log.LogToFile'='true';'Log.LogLevel'='2';'Log.LogFileName'=(Join-Path $GameDirectory 'TSR_FSR411.log')
 'Sharpness.OverrideSharpness'='true';'Sharpness.Sharpness'='0.0';'Sharpness.Shader'='rcas'
 'CAS.Enabled'='false';'CAS.MotionSharpnessEnabled'='false';'CAS.ContrastEnabled'='false';'CAS.SharpenerDebug'='false'
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
foreach($name in @('dxgi.dll','OptiScaler.ini','amd_fidelityfx_upscaler_dx12.dll')){$hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $package $name)).Hash}
@{kind='FSR 4.1.1 API integration test';target=$GameDirectory;hashes=$hashes;changes=$changes;
  neuralModelOwner='AMD';customNeural=$false;sdkSigner=$sig.SignerCertificate.Subject;localPackagePath=$package;created=(Get-Date).ToString('o')} |
 ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $package 'package.json') -Encoding UTF8
Write-Output $package
