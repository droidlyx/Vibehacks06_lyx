# AI providers

The current source project uses **DeepSeek V4.1 Flash 1017 through Qiniu's compatible API** for NPC dialogue and
consequential negotiation. Qwen3 is no longer automatically started or used as a fallback.

## Cloud dialogue configuration

- Base URL: `https://api.qnaigc.com/v1`
- Completion endpoint: `https://api.qnaigc.com/v1/chat/completions`
- Model ID: `deepseek/deepseek-v4.1-flash-1017`
- Authentication: `Authorization: Bearer <key>` using the key issued by this provider, not an unrelated DeepSeek
  account.
- Requests disable thinking for interactive dialogue and use JSON output. The game independently checks the required
  fields, offered action combinations, current distance, authority and resources before applying any action.

Set `DEEPSEEK_API_KEY` in the launching environment, or store only the key in `Local/Secrets/deepseek.key`. The
environment takes precedence. `Local/` is excluded from Git; never put credentials in source, Config, reports or
distributable builds. The portable staging process copies `Local/AI`, not `Local/Secrets`, so each recipient must
provide their own key.

`Open-Editor.cmd`, `Start-Game.ps1` and AI validation import the private key into the process environment before
launching Unreal. They do not print it or put it on the command line. If the editor is already open, reopen it using the
launcher to inherit an environment key, or use the private file in this project.

`Start-Game.ps1 -NoAI` disables AI requests for the launched process. Missing credentials, network failures and rejected
requests show an explicit dialogue status; the game never silently falls back to Qwen. Movement, combat and rule-based
NPC behavior remain available.

## Optional local secondary decisions

StartLux-Decision-2B-Q4_K_M is reserved for occasional bounded secondary choices. It does not generate dialogue or
replace DeepSeek's main negotiation path. Existing routine NPC behavior remains game rules; no recurring StartLux
requests are made by default.

Run `Start-SecondaryAI.ps1` explicitly when needed, and `Stop-SecondaryAI.ps1` to stop the processes owned by that
launcher. The scripts use the existing vendor llama.cpp runtime and original StartLux model package, with local-only
endpoints. `Start-LocalAI.ps1` is a compatibility notice and starts no model.

The secondary launcher's default model and Python paths describe the original development machine. On another machine,
pass `-ModelDirectory`, `-Python` and, if needed, `-Server` for your installed model package, Python environment and
llama.cpp executable. These optional local dependencies are not required for DeepSeek dialogue.

## Validation and existing builds

Rebuild the current editor project, then run:

```powershell
powershell -ExecutionPolicy Bypass -File Tools/Validate-Build.ps1 -Editor -Only AI
```

The validator requires a configured key for AI cases and records the provider/model along with fresh-report and
process-exit checks. It also requires an actual successful cloud-request marker in the fresh engine log, so an old Qwen
binary cannot pass as the new provider. These cases make real API calls.

Existing v0.3 native packages and historical reports were produced with Qwen3. They are not automatically upgraded by
changing the source project; create and validate a new package before distributing the cloud-backed version. The old
Qwen weights may remain on disk but are not used by the updated launchers.
