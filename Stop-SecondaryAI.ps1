$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$statePath = Join-Path $projectRoot 'Saved\AI\StartLux\processes.json'
if (!(Test-Path -LiteralPath $statePath)) {
    Write-Host 'This project has no recorded secondary AI processes.'
    return
}
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
if ($state.ProjectRoot -ine $projectRoot) { throw 'The process record belongs to another project; nothing was stopped.' }
$records = @($state.Processes)
$failures = @()
for ($index = $records.Count - 1; $index -ge 0; $index--) {
    $record = $records[$index]
    if ($record.Role -notin @('llama','decision')) { throw 'Unrecognized process record; no unrecognized process will be stopped.' }
    $process = Get-Process -Id ([int]$record.Id) -ErrorAction SilentlyContinue
    if (!$process) { continue }
    try {
        if ($process.Path -ine $record.Executable -or
            $process.StartTime.ToUniversalTime().Ticks.ToString() -ne $record.StartTimeUtcTicks) {
            Write-Warning "PID $($record.Id) no longer matches the recorded process; left running."
            continue
        }
        # Includes the actual interpreter created by a Windows venv redirector, after root identity validation.
        & (Join-Path $env:SystemRoot 'System32\taskkill.exe') /PID $process.Id /T /F 2>$null | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Process tree termination failed.' }
        Write-Host "Stopped this project's $($record.Role) process ($($record.Id))."
    } catch { $failures += "Could not stop recorded PID $($record.Id)." }
}
if ($failures.Count) { throw ($failures -join ' ') }
Remove-Item -LiteralPath $statePath
