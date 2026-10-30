param([switch]$Package)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$engineRoot=if($env:UE_ROOT){$env:UE_ROOT}else{'E:\UE_5.8'}
$project=Join-Path $root 'Verdant.uproject'
if($Package){
 & (Join-Path $engineRoot 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun "-project=$project" -noP4 -platform=Win64 -clientconfig=Development -build -cook -map=Valley -stage -pak -archive "-archivedirectory=$(Join-Path $root 'Builds')" -utf8output
}else{
 & (Join-Path $engineRoot 'Engine\Build\BatchFiles\Build.bat') VerdantEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE
}
exit $LASTEXITCODE
