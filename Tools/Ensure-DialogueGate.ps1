param([string]$ProjectRoot=(Split-Path $PSScriptRoot))
$ErrorActionPreference='Stop'
# This background bootstrap does not need the cloud credential.
Remove-Item Env:\DEEPSEEK_API_KEY -ErrorAction SilentlyContinue
$projectRoot=[IO.Path]::GetFullPath($ProjectRoot)
$gateMutex=New-Object Threading.Mutex($false,'Local\VerdantStartLux8090Bootstrap')
$ownsMutex=$false
try {
    try { $ownsMutex=$gateMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $ownsMutex=$true }
    if(-not $ownsMutex){return}
    $healthy=$false
    try {
        $health=Invoke-RestMethod -Uri 'http://127.0.0.1:8090/health' -TimeoutSec 2
        if($health.status -eq 'ok' -and $health.model -eq 'verdant-startlux-secondary'){$healthy=$true}
        else {throw 'Port 8090 does not identify the expected StartLux service.'}
    } catch { }
    if($healthy){Write-Output 'StartLux relevance gate is ready.';return}
    $launcher=Join-Path $projectRoot 'Start-SecondaryAI.ps1'
    if(-not(Test-Path -LiteralPath $launcher)){throw 'Start-SecondaryAI.ps1 is missing; cloud dialogue remains gated off.'}
    & $launcher
} finally {
    if($ownsMutex){$gateMutex.ReleaseMutex()}
    $gateMutex.Dispose()
}
