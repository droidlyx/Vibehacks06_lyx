param(
    [string]$ModelDirectory = 'E:\StartLux-Decision-2B-Q4_K_M-GGUF',
    [string]$Python = 'E:\UE_projects\EmberGenerationsUE\Saved\Decision\venv\Scripts\python.exe',
    [string]$Server = (Join-Path $PSScriptRoot 'Local\AI\llama\llama-server.exe'),
    [ValidateRange(1,65535)][int]$LlamaPort = 8081,
    [ValidateRange(1,65535)][int]$Port = 8090,
    [ValidateRange(0,999)][int]$GpuLayers = 99,
    [ValidateRange(10,300)][int]$StartupTimeoutSeconds = 120
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$logRoot = Join-Path $projectRoot 'Saved\AI\StartLux'
$statePath = Join-Path $logRoot 'processes.json'
$created = @()

function Find-OwnedProcess($record) {
    try {
        $process = Get-Process -Id ([int]$record.Id) -ErrorAction Stop
        if ($process.Path -ieq $record.Executable -and
            $process.StartTime.ToUniversalTime().Ticks.ToString() -eq $record.StartTimeUtcTicks) {
            return $process
        }
    } catch { }
    return $null
}

function Save-State {
    $state = [ordered]@{
        ProjectRoot = $projectRoot
        ModelDirectory = $ModelDirectory
        LlamaPort = $LlamaPort
        Port = $Port
        Processes = @($created)
    }
    $temporary = $statePath + '.tmp'
    [IO.File]::WriteAllText($temporary, ($state | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding($false)))
    Move-Item -LiteralPath $temporary -Destination $statePath -Force
}

function Start-OwnedProcess([string]$role, [string]$executable, [string[]]$arguments, [string]$workingDirectory) {
    $process = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $workingDirectory `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $logRoot ($role + '.out.log')) `
        -RedirectStandardError (Join-Path $logRoot ($role + '.err.log'))
    $process.Refresh()
    if ($process.HasExited) { throw "$role exited during startup. See $logRoot" }
    $record = [PSCustomObject]@{
        Role = $role
        Id = $process.Id
        Executable = $process.Path
        StartTimeUtcTicks = $process.StartTime.ToUniversalTime().Ticks.ToString()
    }
    $script:created += $record
    Save-State
}

function Wait-Health([int]$servicePort, [string]$expectedModel = '') {
    $deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($record in $created) {
            if (!(Find-OwnedProcess $record)) { throw "$($record.Role) exited. See $logRoot" }
        }
        try {
            $request = [Net.HttpWebRequest]::Create("http://127.0.0.1:$servicePort/health")
            $request.Proxy = $null
            $request.Timeout = 2000
            $response = $request.GetResponse()
            try {
                $reader = New-Object IO.StreamReader($response.GetResponseStream())
                try { $health = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
                if ($health.status -eq 'ok' -and (!$expectedModel -or $health.model -eq $expectedModel)) { return }
            } finally { $response.Dispose() }
        } catch { }
        Start-Sleep -Milliseconds 250
    }
    throw "Service on port $servicePort did not become ready. See $logRoot"
}

if ($LlamaPort -eq $Port) { throw 'LlamaPort and Port must be different.' }
$ModelDirectory = (Resolve-Path -LiteralPath $ModelDirectory).Path
$Python = (Resolve-Path -LiteralPath $Python).Path
$Server = (Resolve-Path -LiteralPath $Server).Path
$weights = @(Get-ChildItem -LiteralPath $ModelDirectory -Filter '*.gguf' -File)
if ($weights.Count -ne 1) { throw 'ModelDirectory must contain exactly one GGUF file.' }
if (!(Test-Path -LiteralPath (Join-Path $ModelDirectory 'startlux_decision\gguf_server.py'))) {
    throw 'ModelDirectory must include the official startlux_decision package.'
}
if (Test-Path -LiteralPath $statePath) {
    $previous = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    if ($previous.ProjectRoot -ine $projectRoot) { throw 'The existing process record belongs to another project.' }
    foreach ($record in $previous.Processes) {
        if (Find-OwnedProcess $record) { throw 'This project already has a secondary AI process. Run Stop-SecondaryAI.ps1 first.' }
    }
}
foreach ($servicePort in @($LlamaPort, $Port)) {
    $listener = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback, $servicePort)
    try { $listener.Start() } catch { throw "Port $servicePort is occupied. No existing service was reused or stopped." }
    finally { $listener.Stop() }
}
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$environmentBefore = @{}
foreach ($name in @('HF_HUB_OFFLINE','TRANSFORMERS_OFFLINE','PYTHONUTF8','NO_PROXY')) {
    $environmentBefore[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    [Environment]::SetEnvironmentVariable('HF_HUB_OFFLINE', '1', 'Process')
    [Environment]::SetEnvironmentVariable('TRANSFORMERS_OFFLINE', '1', 'Process')
    [Environment]::SetEnvironmentVariable('PYTHONUTF8', '1', 'Process')
    [Environment]::SetEnvironmentVariable('NO_PROXY', '127.0.0.1,localhost', 'Process')
    Start-OwnedProcess 'llama' $Server @('-m', ('"' + $weights[0].FullName + '"'), '-ngl', "$GpuLayers", `
        '-c', '4096', '--parallel', '1', '--threads', '4', '--host', '127.0.0.1', '--port', "$LlamaPort", `
        '--alias', 'verdant-startlux-secondary', '--no-webui') (Split-Path $Server)
    Wait-Health $LlamaPort
    Start-OwnedProcess 'decision' $Python @('-m', 'startlux_decision.gguf_server', '--model-dir', `
        ('"' + $ModelDirectory + '"'), '--llama', "http://127.0.0.1:$LlamaPort", '--port', "$Port", `
        '--workers', '1', '--name', 'verdant-startlux-secondary') $ModelDirectory
    Wait-Health $Port 'verdant-startlux-secondary'
    Write-Host "Secondary decisions ready: http://127.0.0.1:$Port/v1/systemone"
    Write-Host 'Started on demand. Run Stop-SecondaryAI.ps1 when finished.'
} catch {
    for ($index = $created.Count - 1; $index -ge 0; $index--) {
        $owned = Find-OwnedProcess $created[$index]
        if ($owned) {
            # The venv redirector can own a child Python interpreter. Stop its tree only after checking identity.
            try {
                & (Join-Path $env:SystemRoot 'System32\taskkill.exe') /PID $owned.Id /T /F 2>$null | Out-Null
            } catch { Write-Warning "Could not clean up PID $($owned.Id); its identity record is retained for Stop-SecondaryAI.ps1." }
        }
    }
    throw
} finally {
    foreach ($name in $environmentBefore.Keys) {
        [Environment]::SetEnvironmentVariable($name, $environmentBefore[$name], 'Process')
    }
}
