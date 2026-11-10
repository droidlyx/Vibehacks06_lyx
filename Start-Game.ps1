param([switch]$NoAI,[switch]$Editor,[switch]$Smoke)
$ErrorActionPreference='Stop'
$projectRoot=$PSScriptRoot
# Keep this small import inline: portable builds copy this launcher without Tools/.
# Credentials are inherited by the child process, never passed on its command line.
if ($NoAI) {
    $env:VERDANT_DISABLE_AI='1'
} else {
    Remove-Item Env:\VERDANT_DISABLE_AI -ErrorAction SilentlyContinue
    $keyFile=Join-Path $projectRoot 'Local\Secrets\deepseek.key'
    if ([string]::IsNullOrWhiteSpace($env:DEEPSEEK_API_KEY) -and (Test-Path -LiteralPath $keyFile)) {
        $env:DEEPSEEK_API_KEY=[IO.File]::ReadAllText($keyFile).Trim()
    }
    if ([string]::IsNullOrWhiteSpace($env:DEEPSEEK_API_KEY) -or $env:DEEPSEEK_API_KEY -match '\s') {
        Remove-Item Env:\DEEPSEEK_API_KEY -ErrorAction SilentlyContinue
        Write-Host 'DeepSeek API key is missing or invalid. See Docs/LocalAI.md; gameplay remains available.'
    }
}
# Relevance and dialogue run through DeepSeek; launch no local inference service.
$packaged=Join-Path $projectRoot 'Verdant.exe'
if(-not(Test-Path -LiteralPath $packaged)){
    $buildRoot=Join-Path $projectRoot 'Builds'
    $activeFile=Join-Path $buildRoot 'ActiveBuild.txt'
    if(Test-Path -LiteralPath $activeFile){
        $relative=(Get-Content -LiteralPath $activeFile -Raw).Trim()
        $candidate=[IO.Path]::GetFullPath((Join-Path $buildRoot $relative))
        $prefix=[IO.Path]::GetFullPath($buildRoot)+[IO.Path]::DirectorySeparatorChar
        if($candidate.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){
            $activeExe=Join-Path $candidate 'Verdant.exe'
            if(Test-Path -LiteralPath $activeExe){$packaged=$activeExe}
        }
    }
    if(-not(Test-Path -LiteralPath $packaged)){$packaged=Join-Path $projectRoot 'Builds\Windows\Verdant.exe'}
}
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
if($Smoke){Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden}
else{Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $projectRoot}
