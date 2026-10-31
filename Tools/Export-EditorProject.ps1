param([string]$Destination='E:\UE_projects\VerdantAccord_v03')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$dest=[IO.Path]::GetFullPath($Destination)
if(Test-Path -LiteralPath $dest){throw 'Destination already exists; choose a new folder.'}
New-Item -ItemType Directory -Path $dest | Out-Null
foreach($folder in @('Source','Config','Content','Build','Docs','Assets','Tools')){
 Copy-Item -LiteralPath (Join-Path $root $folder) -Destination $dest -Recurse
}
foreach($file in @('Verdant.uproject','README.md','Play.cmd','Start-Game.ps1','Start-LocalAI.ps1','Open-Editor.cmd','.gitignore','.gitattributes')){
 Copy-Item -LiteralPath (Join-Path $root $file) -Destination $dest
}
New-Item -ItemType Directory -Path (Join-Path $dest 'Local') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'Local\AI') -Destination (Join-Path $dest 'Local') -Recurse
New-Item -ItemType Directory -Path (Join-Path $dest 'Binaries\Win64') -Force | Out-Null
foreach($file in @('UnrealEditor-Verdant.dll','UnrealEditor-Verdant.pdb','UnrealEditor.modules','VerdantEditor.target')){
 Copy-Item -LiteralPath (Join-Path $root ('Binaries\Win64\'+$file)) -Destination (Join-Path $dest 'Binaries\Win64')
}
Write-Output ('Independent editor project exported: '+(Join-Path $dest 'Verdant.uproject'))

