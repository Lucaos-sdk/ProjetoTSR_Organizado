@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\..\tools\restore_witcher_probe.ps1" -Backup "C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12\TSR-backup-20260906-193736-184"
pause
