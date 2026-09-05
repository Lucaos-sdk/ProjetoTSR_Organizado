# Run from any directory: powershell -File .\tools\Update-Local.ps1
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
$branch = 'fix/tsr-validation-and-safe-fallback'
function Invoke-Git {
    & git -C $repoPath @args
    if ($LASTEXITCODE -ne 0) { throw "Git failed. Your local changes were not reset." }
}
$origin = Invoke-Git remote get-url origin
if ($origin.Trim() -notmatch '^(https://github\.com/|git@github\.com:)Lucaos-sdk/ProjetoTSR_Organizado(\.git)?/?$') {
    throw 'Unexpected origin. Verify that this is the ProjetoTSR_Organizado clone.'
}
$dirty = Invoke-Git status --porcelain
if ($dirty) {
    throw 'Local changes or untracked files found. Save/commit them before updating; no files were overwritten.'
}
Invoke-Git fetch origin "refs/heads/${branch}:refs/remotes/origin/${branch}"
& git -C $repoPath show-ref --verify --quiet "refs/heads/$branch"
if ($LASTEXITCODE -eq 0) {
    Invoke-Git switch $branch
} elseif ($LASTEXITCODE -eq 1) {
    Invoke-Git switch --create $branch --track "origin/$branch"
} else {
    throw 'Could not inspect local branch.'
}
Invoke-Git merge --ff-only "origin/$branch"
Invoke-Git log -1 --oneline
Write-Host 'Updated successfully. Build instructions: native/baseline/README.md'
