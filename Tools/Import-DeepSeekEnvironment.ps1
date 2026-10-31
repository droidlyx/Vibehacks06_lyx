param(
    [string]$ProjectRoot=(Split-Path $PSScriptRoot),
    [switch]$RequireKey,
    [switch]$OpenEditor
)
$ErrorActionPreference='Stop'
# Dot-source this script to prepare credentials for a launched child process.
# Never print the credential or pass it as a command-line argument.
Remove-Item Env:\VERDANT_DISABLE_AI -ErrorAction SilentlyContinue
$keyFile=Join-Path $ProjectRoot 'Local\Secrets\deepseek.key'
if ([string]::IsNullOrWhiteSpace($env:DEEPSEEK_API_KEY) -and (Test-Path -LiteralPath $keyFile)) {
    $env:DEEPSEEK_API_KEY=[IO.File]::ReadAllText($keyFile).Trim()
}
$keyReady=-not [string]::IsNullOrWhiteSpace($env:DEEPSEEK_API_KEY) -and $env:DEEPSEEK_API_KEY -notmatch '\s'
if (-not $keyReady) {
    Remove-Item Env:\DEEPSEEK_API_KEY -ErrorAction SilentlyContinue
    if ($RequireKey) { throw 'A valid DeepSeek API key is required. Configure DEEPSEEK_API_KEY or Local/Secrets/deepseek.key.' }
    Write-Host 'DeepSeek API key is missing or invalid. See Docs/LocalAI.md; gameplay remains available.'
}
if ($OpenEditor) {
    $gateLog=Join-Path $ProjectRoot 'Saved\AI'
    New-Item -ItemType Directory -Path $gateLog -Force | Out-Null
    $gateBootstrap=Join-Path $PSScriptRoot 'Ensure-DialogueGate.ps1'
    if($keyReady -and (Test-Path -LiteralPath $gateBootstrap)) {
        Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$gateBootstrap+'"'),'-ProjectRoot',('"'+$ProjectRoot+'"')) -WindowStyle Hidden -RedirectStandardOutput (Join-Path $gateLog 'gate-bootstrap.out.log') -RedirectStandardError (Join-Path $gateLog 'gate-bootstrap.err.log')
    }
    $engineRoot=if($env:UE_ROOT){$env:UE_ROOT}else{'E:\UE_5.8'}
    $editorExe=Join-Path $engineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
    if (-not (Test-Path -LiteralPath $editorExe)) { throw 'Unreal Editor is missing. Set UE_ROOT to the engine directory.' }
    Start-Process -FilePath $editorExe -ArgumentList @(('"'+(Join-Path $ProjectRoot 'Verdant.uproject')+'"')) -WorkingDirectory $ProjectRoot
}
