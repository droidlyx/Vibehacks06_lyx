param(
 [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$Version,
 [switch]$Editor,[switch]$FullJourney,[switch]$StewardJourney,
 [ValidateSet('Smoke','World','NPC','Social','Combat','AI','FullJourney','StewardJourney')][string[]]$Only
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
if($Editor){
 $package=$root
 $engineRoot=if($env:UE_ROOT){$env:UE_ROOT}else{'E:\UE_5.8'}
 $exe=Join-Path $engineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
 $saved=Join-Path $root 'Saved'
 $baseArguments=@(('"'+(Join-Path $root 'Verdant.uproject')+'"'),'/Game/Maps/Valley','-game')
}else{
 if(-not $Version){throw 'Specify -Version for a native package, or -Editor for the source project.'}
 $package=Join-Path $root ('Builds\'+$Version+'\Windows')
 $exe=Join-Path $package 'Verdant\Binaries\Win64\Verdant.exe'
 $saved=Join-Path $package 'Verdant\Saved'
 $baseArguments=@()
}
if(-not(Test-Path -LiteralPath $exe)){throw 'The requested game executable is missing.'}
$logDir=Join-Path $saved 'Logs';New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$reportDir=Join-Path $saved 'Validation'
$cases=@(
 @{Name='Smoke';Flag='-VerdantSmoke';Size='1600x900';Timeout=110;Extra=@('-RenderOffscreen');Report='smoke-results.txt'},
 @{Name='World';Flag='-VerdantWorldProbe';Size='1280x720';Timeout=110;Extra=@('-NullRHI','-Deterministic','-FPS=60');Report='world-probe-results.txt'},
 @{Name='NPC';Flag='-VerdantNPCProbe';Size='1280x720';Timeout=125;Extra=@('-NullRHI','-Deterministic','-FPS=60');Report='npc-probe.json'},
 @{Name='Social';Flag='-VerdantSocialProbe';Size='1280x720';Timeout=70;Extra=@('-NullRHI','-Deterministic','-FPS=60');Report='social-probe.json'},
 @{Name='Combat';Flag='-VerdantCombatProbe';Size='1280x720';Timeout=115;Extra=@('-RenderOffscreen');Report='combat-probe.json'},
 @{Name='AI';Flag='-VerdantAIProbe';Size='1280x720';Timeout=150;Extra=@('-RenderOffscreen');Report='ai-game-results.txt'}
)
if($FullJourney -or ($Only -contains 'FullJourney')){$cases+=@{Name='FullJourney';Flag='-VerdantInputJourney';Size='1280x720';Timeout=350;Extra=@('-RenderOffscreen','-VerdantFullJourney');Report='full-journey.json'}}
if($StewardJourney -or ($Only -contains 'StewardJourney')){$cases+=@{Name='StewardJourney';Flag='-VerdantStewardJourney';Size='1280x720';Timeout=350;Extra=@('-RenderOffscreen');Report='steward-journey.json'}}
if($Only){$cases=@($cases | Where-Object {$Only -contains $_.Name})}
$needsAI=@($cases | Where-Object {$_.Name -in @('AI','FullJourney','StewardJourney')}).Count -gt 0
if($needsAI){
 $healthy=$false
 try{$null=Invoke-RestMethod -Uri 'http://127.0.0.1:8066/health' -TimeoutSec 2;$healthy=$true}catch{}
 if(-not $healthy){
  $ai=Join-Path $package 'Local\AI';$server=Join-Path $ai 'llama\llama-server.exe';$model=Join-Path $ai 'Qwen3-1.7B-Q8_0.gguf'
  if(-not(Test-Path -LiteralPath $server) -or -not(Test-Path -LiteralPath $model)){throw 'Stage the local AI before validating these cases.'}
  $aiLogs=Join-Path $package 'Saved\AI';New-Item -ItemType Directory -Path $aiLogs -Force | Out-Null
  $serverArguments=@('-m',('"'+$model+'"'),'--host','127.0.0.1','--port','8066','-ngl','99','-c','8192','--parallel','1','--jinja','--alias','verdant-local','--no-webui')
  $null=Start-Process -FilePath $server -ArgumentList $serverArguments -WorkingDirectory (Split-Path $server) -WindowStyle Hidden -RedirectStandardOutput (Join-Path $aiLogs 'validation-server.out.log') -RedirectStandardError (Join-Path $aiLogs 'validation-server.err.log') -PassThru
  $deadline=(Get-Date).AddSeconds(45)
  while(-not $healthy -and (Get-Date) -lt $deadline){Start-Sleep -Seconds 1;try{$null=Invoke-RestMethod -Uri 'http://127.0.0.1:8066/health' -TimeoutSec 2;$healthy=$true}catch{}}
  if(-not $healthy){throw 'The local AI did not become ready; inspect its log.'}
 }
}
function Expand-RunRecords($node){
 if($node -is [System.Array]){foreach($entry in $node){Expand-RunRecords $entry}}
 elseif($null -ne $node.name){$node}
 elseif($null -ne $node.value){Expand-RunRecords $node.value}
}
$summary=Join-Path $logDir 'validation-runs.json';$results=@()
if(Test-Path -LiteralPath $summary){
 $parsed=Get-Content -LiteralPath $summary -Raw | ConvertFrom-Json
 $latest=@{};foreach($record in (Expand-RunRecords $parsed)){$latest[$record.name]=$record}
 $results=@($latest.Values)
}
foreach($case in $cases){
 $size=$case.Size.Split('x')
 $launchArguments=$baseArguments+@($case.Flag,'-windowed',('-ResX='+$size[0]),('-ResY='+$size[1]),'-ForceRes','-unattended','-NoSplash','-nosound',('-abslog="'+(Join-Path $logDir ('Validation-'+$case.Name+'.log'))+'"'))+$case.Extra
 $began=(Get-Date).ToUniversalTime()
 Write-Output ('Starting hidden '+$case.Name+'.')
 $p=Start-Process -FilePath $exe -ArgumentList $launchArguments -WorkingDirectory $package -WindowStyle Hidden -PassThru
 $timedOut=-not $p.WaitForExit($case.Timeout*1000)
 if($timedOut){Stop-Process -Id $p.Id;$code=124}else{$p.Refresh();$code=$p.ExitCode}
 $report=Join-Path $reportDir $case.Report;$reportedFailures=-1;$fresh=$false
 if(Test-Path -LiteralPath $report){
  $fresh=(Get-Item -LiteralPath $report).LastWriteTimeUtc -ge $began.AddSeconds(-1)
  $body=Get-Content -LiteralPath $report -Raw -Encoding UTF8
  if($report.EndsWith('.json')){
   $result=$body | ConvertFrom-Json
   if($null -ne $result.failures){$reportedFailures=[int]$result.failures}
  }elseif($body -match '(?m)^Failures: (\d+)\s*$'){$reportedFailures=[int]$Matches[1]}
 }
 $passed=($code -eq 0 -and -not $timedOut -and $fresh -and $reportedFailures -eq 0)
 $results=@($results | Where-Object {$_.name -ne $case.Name})
 $results+=@{name=$case.Name;exit_code=$code;reported_failures=$reportedFailures;report_fresh=$fresh;passed=$passed;timed_out=$timedOut;viewport=$case.Size;profiler_enabled=$false;editor=[bool]$Editor;completed_utc=(Get-Date).ToUniversalTime().ToString('o')}
 ConvertTo-Json -InputObject $results -Depth 6 | Set-Content -LiteralPath $summary -Encoding UTF8
 if(-not $passed){throw ($case.Name+' failed: process='+$code+', reported failures='+$reportedFailures+', fresh report='+$fresh+'. Inspect logs and artifacts.')}
 Write-Output ($case.Name+' passed, including its fresh report.')
}
Write-Output 'Hidden validation finished; reports and process exit codes both passed. No physical desktop input was used.'
