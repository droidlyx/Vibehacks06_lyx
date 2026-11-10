$ErrorActionPreference='Stop'
$target=Join-Path $PSScriptRoot 'Grow_up_final-Windows.zip'
if(Test-Path -LiteralPath $target){throw 'ZIP already exists. Move it elsewhere before joining again.'}
$entries=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'parts.sha256')
foreach($entry in $entries){
 $hash,$name=$entry -split '  ',2
 $part=Join-Path $PSScriptRoot $name
 if(-not(Test-Path -LiteralPath $part)){throw ('Missing part: '+$name)}
 if((Get-FileHash -LiteralPath $part -Algorithm SHA256).Hash -ne $hash){throw ('Invalid part (download the LFS file, not its pointer): '+$name)}
}
$output=[IO.File]::Open($target,[IO.FileMode]::CreateNew)
try {
 foreach($entry in $entries){
  $hash,$name=$entry -split '  ',2
  $inputFile=[IO.File]::OpenRead((Join-Path $PSScriptRoot $name))
  try {$inputFile.CopyTo($output)} finally {$inputFile.Dispose()}
 }
} finally {$output.Dispose()}
$expected=(Get-Content -LiteralPath (Join-Path $PSScriptRoot 'SHA256SUMS.txt')).Split(' ')[0]
if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $expected){throw 'Joined ZIP checksum mismatch.'}
Write-Host 'ZIP verified. Extract it, then launch Grow_up_final/Grow_up.exe.'
