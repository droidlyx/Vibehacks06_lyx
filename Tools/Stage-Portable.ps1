param(
 [Parameter(Mandatory=$true)][string]$Version,
 [switch]$NoAI,
 [string]$PackageDirectory
)
$ErrorActionPreference='Stop'
if($Version -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$'){throw 'Use a version folder name, not a path.'}
$root=Split-Path $PSScriptRoot
$dest=if($PackageDirectory){[IO.Path]::GetFullPath($PackageDirectory)}else{Join-Path $root ('Builds\'+$Version+'\Windows')}
if(-not(Test-Path -LiteralPath (Join-Path $dest 'Verdant.exe'))){throw 'Package the version before staging the portable files.'}

# Stage only into a fresh cook/archive, never a previously played or model-bundled copy.
# Do not delete user files to turn a used package into a distributable package.
foreach($relative in @('Local','Verdant\Local','Saved','Verdant\Saved','.env')){
 if(Test-Path -LiteralPath (Join-Path $dest $relative)){
  throw ('Package contains local/runtime data at '+$relative+'. Use a fresh version archive before distribution.')
 }
}
$packageAssets=Join-Path $dest 'Assets'
if(Test-Path -LiteralPath $packageAssets){
 $rawAudio=@(Get-ChildItem -LiteralPath $packageAssets -Recurse -File | Where-Object {$_.Extension -in @('.wav','.mp3','.ogg','.flac','.aiff','.aif')})
 if($rawAudio.Count){throw 'Package contains loose audio source files. Use a fresh archive; only cooked audio is distributed.'}
}

# Start-Game.ps1 imports the recipient's credentials inline. It has no Tools/ dependency
# and does not launch Qwen/StartLux. Keep -NoAI for compatibility with existing callers;
# recipients choose offline play with Start-Game.ps1 -NoAI, not a different asset bundle.
$files=@(
 'Play.cmd','Start-Game.ps1',
 'Docs/LocalAI.md','Docs/InteractionDesign.zh-CN.md','Docs/FoodGrowthDesign.zh-CN.md',
 'Docs/CharacterCatalog/model_sources.json',
 'Assets/README.md','Assets/manifest.json','Assets/provenance.json',
 'Assets/Source/Quaternius/UAL1_LICENSE.txt','Assets/Source/Quaternius/UAL2_LICENSE.txt',
 'Assets/Source/Quaternius/outfits/License_Standard.txt',
 'Assets/Source/Quaternius/nature/License_Standard.txt',
 'Assets/Source/Quaternius/base/License_Standard.txt',
 'Assets/Source/Quaternius/Ecology/License_Standard.txt',
 'Assets/Environment/ecology-assets.json',
 'Assets/Titan/README.md','Assets/Titan/manifest.json','Assets/Titan/motion-report.json',
 'Assets/TitanSky/manifest.json',
 'Assets/Audio/README.md','Assets/Audio/manifest.json','Assets/Audio/runtime-audio-manifest.json',
 'Assets/Audio/ThirdParty/Kenney/RPG/License.txt','Assets/Audio/ThirdParty/Kenney/RPG/provenance.json',
 'Assets/Audio/Suno/generation-session.json','Assets/Audio/Suno/Originals/VA01_Mosslit_Paths.metadata.json'
)
$copies=@($files | ForEach-Object { @{Source=(Join-Path $root $_); Destination=$_} })
$copies+=@{Source=(Join-Path $PSScriptRoot 'Portable-README.md'); Destination='README.md'}
$copies+=@{Source=(Join-Path $PSScriptRoot 'Portable-THIRD-PARTY-NOTICES.txt'); Destination='THIRD-PARTY-NOTICES.txt'}
$copies+=@{Source=(Join-Path $PSScriptRoot 'Portable-TomMusic-License.txt'); Destination='Assets/Audio/ThirdParty/TomMusic/License.txt'}
foreach($entry in $copies){
 if(-not(Test-Path -LiteralPath $entry.Source -PathType Leaf)){throw ('Required portable file is missing: '+$entry.Source)}
}

$records=@()
foreach($entry in $copies){
 $target=Join-Path $dest $entry.Destination
 New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
 Copy-Item -LiteralPath $entry.Source -Destination $target -Force
 $records+=@{path=$entry.Destination;sha256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()}
}
@{schema_version=1;version=$Version;staged_utc=(Get-Date).ToUniversalTime().ToString('o');
 credential_included=$false;local_model_included=$false;raw_audio_included=$false;support_files=$records} |
 ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $dest 'portable-files.json') -Encoding UTF8
Write-Host ('Portable support files staged in '+$dest+'. Package launch and manual gameplay review remain separate checks.')
