param([Parameter(Mandatory=$true)][string]$Version,[switch]$NoAI)
$ErrorActionPreference='Stop'
if($Version -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$'){throw 'Use a version folder name, not a path.'}
$root=Split-Path $PSScriptRoot
$dest=Join-Path $root ('Builds\'+$Version+'\Windows')
if(-not(Test-Path -LiteralPath (Join-Path $dest 'Verdant.exe'))){throw 'Package the version before staging the portable files.'}
foreach($file in @('Play.cmd','Start-Game.ps1','README.md')){Copy-Item -LiteralPath (Join-Path $root $file) -Destination $dest -Force}
Copy-Item -LiteralPath (Join-Path $root 'Docs') -Destination $dest -Recurse -Force
$assets=Join-Path $dest 'Assets'; New-Item -ItemType Directory -Path $assets -Force | Out-Null
foreach($file in @('README.md','manifest.json','provenance.json','generation-ledger.json')){Copy-Item -LiteralPath (Join-Path $root ('Assets\'+$file)) -Destination $assets -Force}
foreach($relative in @('Source\Quaternius\UAL1_LICENSE.txt','Source\Quaternius\UAL2_LICENSE.txt','Source\Quaternius\outfits\License_Standard.txt','Source\Quaternius\nature\License_Standard.txt','Source\Quaternius\base\License_Standard.txt','Audio\README.md','Audio\manifest.json')){
 $target=Join-Path $assets $relative
 New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
 Copy-Item -LiteralPath (Join-Path $root ('Assets\'+$relative)) -Destination $target -Force
}
if(-not $NoAI){
 $local=Join-Path $dest 'Local'; New-Item -ItemType Directory -Path $local -Force | Out-Null
 Copy-Item -LiteralPath (Join-Path $root 'Local\AI') -Destination $local -Recurse -Force
 $meta=Get-Content -LiteralPath (Join-Path $local 'AI\provenance.json') -Raw | ConvertFrom-Json
 $hash=(Get-FileHash -LiteralPath (Join-Path $local ('AI\'+$meta.model_file)) -Algorithm SHA256).Hash
 if($hash -ne $meta.sha256){throw 'Portable model hash does not match its provenance.'}
}
Write-Host ('Portable files staged in '+$dest+'. Validate the executable before changing Builds/ActiveBuild.txt.')
