@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-LocalAI.ps1"
start "" "E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0Verdant.uproject"
