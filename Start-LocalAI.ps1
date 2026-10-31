$ErrorActionPreference='Stop'
$ai=Join-Path $PSScriptRoot 'Local\AI'
try { $null=Invoke-RestMethod 'http://127.0.0.1:8066/health' -TimeoutSec 2; exit 0 } catch {}
$logs=Join-Path $PSScriptRoot 'Saved\AI'
New-Item -ItemType Directory -Path $logs -Force | Out-Null
$argsAI=@('-m',('"'+(Join-Path $ai 'Qwen3-1.7B-Q8_0.gguf')+'"'),'--host','127.0.0.1','--port','8066','-ngl','99','-c','8192','--parallel','1','--jinja','--alias','verdant-local','--no-webui')
Start-Process -FilePath (Join-Path $ai 'llama\llama-server.exe') -ArgumentList $argsAI -WindowStyle Hidden -RedirectStandardOutput (Join-Path $logs 'editor-server.out.log') -RedirectStandardError (Join-Path $logs 'editor-server.err.log')
