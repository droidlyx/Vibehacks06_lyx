param([switch]$NoAI,[switch]$Editor,[switch]$Smoke)
$ErrorActionPreference='Stop'
$projectRoot=$PSScriptRoot
$aiRoot=Join-Path $projectRoot 'Local\AI'
if (-not $NoAI) {
    $healthy=$false
    try { $null=Invoke-RestMethod -Uri 'http://127.0.0.1:8066/health' -TimeoutSec 2; $healthy=$true } catch {}
    if (-not $healthy) {
        $server=Join-Path $aiRoot 'llama\llama-server.exe'
        $model=Join-Path $aiRoot 'Qwen3-1.7B-Q8_0.gguf'
        if ((Test-Path -LiteralPath $server) -and (Test-Path -LiteralPath $model)) {
            $logDir=Join-Path $projectRoot 'Saved\AI'; New-Item -ItemType Directory -Force -Path $logDir | Out-Null
            $arguments=@('-m',('"'+$model+'"'),'--host','127.0.0.1','--port','8066','-ngl','99','-c','8192','--parallel','1','--jinja','--alias','verdant-local','--no-webui')
            $null=Start-Process -FilePath $server -ArgumentList $arguments -WorkingDirectory (Split-Path $server) -WindowStyle Hidden -RedirectStandardOutput (Join-Path $logDir 'server.out.log') -RedirectStandardError (Join-Path $logDir 'server.err.log') -PassThru
            Write-Host 'Local AI is starting in the background. The game can run while it warms up.'
        } else { Write-Host 'Local AI is not installed. See Docs/LocalAI.md; gameplay remains available.' }
    }
}
$packaged=Join-Path $projectRoot 'Builds\Windows\Verdant.exe'
if ((Test-Path -LiteralPath $packaged) -and -not $Editor) {
    $exe=$packaged
    $arguments=@('-windowed','-ResX=1600','-ResY=900','-log')
} else {
    $engineRoot=if($env:UE_ROOT){$env:UE_ROOT}else{'E:\UE_5.8'}
    $exe=Join-Path $engineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
    $arguments=@(('"'+(Join-Path $projectRoot 'Verdant.uproject')+'"'),'/Game/Maps/Valley','-game','-windowed','-ResX=1600','-ResY=900','-log','-NoSplash')
}
if($Smoke){$arguments+=@('-VerdantSmoke','-RenderOffscreen','-unattended')}
if(-not(Test-Path -LiteralPath $exe)){throw 'Build missing. Set UE_ROOT or create the packaged Windows build.'}
Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden
