param([Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$Version,[switch]$FullJourney,[ValidateSet('Smoke','World','Combat','AI','FullJourney')][string[]]$Only)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$package=Join-Path $root ('Builds\'+$Version+'\Windows')
$exe=Join-Path $package 'Verdant\Binaries\Win64\Verdant.exe'
if(-not(Test-Path -LiteralPath $exe)){throw 'Native packaged executable is missing.'}
$logDir=Join-Path $package 'Verdant\Saved\Logs'; New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$healthy=$false
try{$null=Invoke-RestMethod -Uri 'http://127.0.0.1:8066/health' -TimeoutSec 2;$healthy=$true}catch{}
if(-not $healthy){
 $ai=Join-Path $package 'Local\AI';$server=Join-Path $ai 'llama\llama-server.exe';$model=Join-Path $ai 'Qwen3-1.7B-Q8_0.gguf'
 if(-not(Test-Path -LiteralPath $server)-or-not(Test-Path -LiteralPath $model)){throw 'Stage the portable local AI before validating this build.'}
 $aiLogs=Join-Path $package 'Saved\AI';New-Item -ItemType Directory -Path $aiLogs -Force | Out-Null
 $serverArgs=@('-m',('"'+$model+'"'),'--host','127.0.0.1','--port','8066','-ngl','99','-c','8192','--parallel','1','--jinja','--alias','verdant-local','--no-webui')
 $null=Start-Process -FilePath $server -ArgumentList $serverArgs -WorkingDirectory (Split-Path $server) -WindowStyle Hidden -RedirectStandardOutput (Join-Path $aiLogs 'validation-server.out.log') -RedirectStandardError (Join-Path $aiLogs 'validation-server.err.log') -PassThru
 $deadline=(Get-Date).AddSeconds(45)
 while(-not $healthy -and (Get-Date)-lt $deadline){Start-Sleep -Seconds 1;try{$null=Invoke-RestMethod -Uri 'http://127.0.0.1:8066/health' -TimeoutSec 2;$healthy=$true}catch{}}
 if(-not $healthy){throw 'The portable local AI did not become ready; inspect Saved/AI/validation-server.err.log.'}
}
$cases=@(
 @{Name='Smoke'; Flag='-VerdantSmoke'; Size='1600x900'; Timeout=110; Extra=@('-RenderOffscreen','-nosound')},
 @{Name='World'; Flag='-VerdantWorldProbe'; Size='1280x720'; Timeout=110; Extra=@('-NullRHI','-nosound','-Deterministic','-FPS=60')},
 @{Name='Combat'; Flag='-VerdantCombatProbe'; Size='1280x720'; Timeout=110; Extra=@('-RenderOffscreen','-nosound')},
 @{Name='AI'; Flag='-VerdantAIProbe'; Size='1600x900'; Timeout=100; Extra=@('-RenderOffscreen')}
)
if($FullJourney -or ($Only -contains 'FullJourney')){$cases+=@{Name='FullJourney'; Flag='-VerdantInputJourney'; Size='1280x720'; Timeout=350; Extra=@('-RenderOffscreen','-VerdantFullJourney')}}
if($Only){$cases=@($cases | Where-Object {$Only -contains $_.Name})}
$results=@()
$summary=Join-Path $logDir 'validation-runs.json'
function Expand-RunRecords($node){
 if($node -is [System.Array]){foreach($entry in $node){Expand-RunRecords $entry}}
 elseif($null -ne $node.name){$node}
 elseif($null -ne $node.value){Expand-RunRecords $node.value}
}
if(Test-Path -LiteralPath $summary){
 $parsed=Get-Content -LiteralPath $summary -Raw | ConvertFrom-Json
 $latest=@{};foreach($record in (Expand-RunRecords $parsed)){$latest[$record.name]=$record}
 $results=@($latest.Values)
}
foreach($case in $cases){
 $size=$case.Size.Split('x')
 $args=@($case.Flag,'-windowed',('-ResX='+$size[0]),('-ResY='+$size[1]),'-ForceRes','-unattended','-NoSplash',('-abslog="'+(Join-Path $logDir ('Validation-'+$case.Name+'.log'))+'"'))+$case.Extra
 Write-Output ('Starting '+$case.Name+' in native packaged executable.')
 $p=Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $package -WindowStyle Hidden -PassThru
 $timedOut=-not $p.WaitForExit($case.Timeout*1000)
 if($timedOut){Stop-Process -Id $p.Id; $code=124}else{$p.Refresh();$code=$p.ExitCode}
 $results=@($results | Where-Object {$_.name -ne $case.Name})
 $results+=@{name=$case.Name;exit_code=$code;timed_out=$timedOut;viewport=$case.Size;profiler_enabled=$false;completed_utc=(Get-Date).ToUniversalTime().ToString('o')}
 ConvertTo-Json -InputObject $results -Depth 6 | Set-Content -LiteralPath $summary -Encoding UTF8
 if($code -ne 0){throw ($case.Name+' failed with exit '+$code+'. Inspect the log and Saved/Validation before proceeding.')}
 Write-Output ($case.Name+' passed.')
}
Write-Output 'Native package validation finished. These probes use engine input, not physical desktop control.'
