@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\..\tools\set_witcher_visual_profile.ps1" -Mode Restore
pause
