param([switch]$Package,[ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$Version)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$engineRoot=if($env:UE_ROOT){$env:UE_ROOT}else{'E:\UE_5.8'}
$project=Join-Path $root 'Verdant.uproject'
if($Package){
 if(-not $Version){$Version='dev-'+(Get-Date -Format 'yyyyMMdd-HHmmss')}
 $archive=Join-Path $root ('Builds\'+$Version)
 if(Test-Path -LiteralPath (Join-Path $archive 'Windows\Verdant.exe')){throw 'This version already has a package. Choose a new -Version to preserve the playable build.'}
 & (Join-Path $engineRoot 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun "-project=$project" -noP4 -platform=Win64 -clientconfig=Development -build -cook -map=Valley -stage -pak -archive "-archivedirectory=$archive" -utf8output
}else{
 & (Join-Path $engineRoot 'Engine\Build\BatchFiles\Build.bat') VerdantEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE -gather
}
exit $LASTEXITCODE
