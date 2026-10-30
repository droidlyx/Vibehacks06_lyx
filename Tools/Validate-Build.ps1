param([Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$Version,[switch]$FullJourney)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$package=Join-Path $root ('Builds\'+$Version+'\Windows')
$exe=Join-Path $package 'Verdant\Binaries\Win64\Verdant.exe'
if(-not(Test-Path -LiteralPath $exe)){throw 'Native packaged executable is missing.'}
$logDir=Join-Path $package 'Verdant\Saved\Logs'; New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$cases=@(
 @{Name='Smoke'; Flag='-VerdantSmoke'; Size='1600x900'; Timeout=110; Extra=@('-RenderOffscreen','-nosound')},
 @{Name='World'; Flag='-VerdantWorldProbe'; Size='1280x720'; Timeout=110; Extra=@('-NullRHI','-nosound','-Deterministic','-FPS=60')},
 @{Name='Combat'; Flag='-VerdantCombatProbe'; Size='1280x720'; Timeout=110; Extra=@('-RenderOffscreen','-nosound')},
 @{Name='AI'; Flag='-VerdantAIProbe'; Size='1600x900'; Timeout=100; Extra=@('-RenderOffscreen')}
)
if($FullJourney){$cases+=@{Name='FullJourney'; Flag='-VerdantInputJourney'; Size='1280x720'; Timeout=350; Extra=@('-RenderOffscreen','-VerdantFullJourney','-csvCaptureFrames=3600','-csvGpuStats')}}
$results=@()
foreach($case in $cases){
 $size=$case.Size.Split('x')
 $args=@($case.Flag,'-windowed',('-ResX='+$size[0]),('-ResY='+$size[1]),'-ForceRes','-unattended','-NoSplash',('-abslog="'+(Join-Path $logDir ('Validation-'+$case.Name+'.log'))+'"'))+$case.Extra
 Write-Output ('Starting '+$case.Name+' in native packaged executable.')
 $p=Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $package -WindowStyle Hidden -PassThru
 $timedOut=-not $p.WaitForExit($case.Timeout*1000)
 if($timedOut){Stop-Process -Id $p.Id; $code=124}else{$p.Refresh();$code=$p.ExitCode}
 $results+=@{name=$case.Name;exit_code=$code;timed_out=$timedOut;viewport=$case.Size}
 $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $logDir 'validation-runs.json') -Encoding UTF8
 if($code -ne 0){throw ($case.Name+' failed with exit '+$code+'. Inspect the log and Saved/Validation before proceeding.')}
 Write-Output ($case.Name+' passed.')
}
Write-Output 'Native package validation finished. These probes use engine input, not physical desktop control.'
