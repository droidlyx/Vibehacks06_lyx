# AI providers

The current migration uses **DeepSeek for relevance and dialogue in one request per eligible NPC**. There is no separate
local or cloud relevance-gate request. StartLux and Qwen are not needed by the updated dialogue path; ordinary movement,
combat and resource behaviour continue to use game rules.

The final response contract is: irrelevant input returns literal `无关` and ends silently; relevant input returns
`say/action/target/amount` JSON in the same inference. This final protocol has compiled successfully and passed three
focused API checks; integrated gameplay review remains pending.

## Cloud configuration

- Endpoint: `https://api.qnaigc.com/v1/chat/completions`
- Model: `deepseek/deepseek-v4.1-flash-1017`
- Temperature `.3`, thinking disabled, `max_tokens=256`, non-streaming, no `stop` parameter.
- Do not send `response_format`: the provider returned HTTP400 for `json_object`. The prompt and strict response
  validation enforce the JSON branch instead.
- A successful literal `无关` response is silent. Network errors, empty responses, invalid JSON or truncated responses
  are not treated as
  irrelevant or as permission to change the world.

Set `DEEPSEEK_API_KEY` in the launching environment, or store only the key in `Local/Secrets/deepseek.key`. The
environment takes precedence. `Local/` is excluded from Git. Never put credentials in source, Config, reports or
distributable builds. Each recipient configures their own key.

`Open-Editor.cmd` and `Start-Game.ps1` prepare credentials without printing them or putting them on the command line.
`Start-Game.ps1 -NoAI` disables model requests. Neither normal entry point starts a local model.
`Tools/Stage-Portable.ps1` no longer copies local inference executables, gate bootstrap scripts or model weights into
new portable builds, and never copies the key.

## Candidate filtering and budgets

Before any API call, the speaker and listener must be valid and alive, within 40 metres in three dimensions, and have an
actual height ratio no greater than 5, inclusive. The same eligibility is checked again before applying a reply. Config lives in
`[Verdant.Dialogue]`: `RadiusCm=4000`, `MaxHeightRatio=5`, `MaxListenersPerSpeech=3`.

Direct speech requests only the selected NPC once. Broadcast considers at most 3 initial eligible listeners; with no
listeners it shows only the player's bubble and makes no API call. Player threads allow at most 3 answers without
further bystander replies; proactive NPC chains allow at most 2 actual replies. Only one DeepSeek request runs at a
time.
`无关` responses publish no line or memory and create no new speech event. These limits reduce call/output volume;
input tokens still incur usage.

## Local process cleanup

The current project's StartLux decision and llama processes were identity-checked and stopped. A stale Qwen3-1.7B-Q8_0
process from the earlier `Vibehacks06_lyx/Builds/v0.3` build was also verified by executable path, model and port before
stopping. Ports 8066, 8081 and 8090 had no listeners after cleanup. Model files remain on disk.

`Start-SecondaryAI.ps1` and `Stop-SecondaryAI.ps1` remain as manual legacy utilities; normal launchers do not call them.
`Start-LocalAI.ps1` is a compatibility notice and starts no model. Manually starting those old services is unnecessary
for the current cloud-only path and can consume GPU memory again.

## Validation and old builds

The earlier 2x height policy passed 25 focused native distance/height boundary checks
(`Saved/Validation/cloud-dialogue-switch-policy.json`). Six real cloud checks passed for the immediately preceding
protocol that returned the text “不相关”; they are retained as historical results. The final literal `无关` protocol
compiled (`Saved/cloud-dialogue-final-build.log`) and passed 3/3 targeted API cases
(`Saved/Validation/cloud-dialogue-switch-wuguan-live.json`), each with one request and no retry. The two irrelevant
cases
used one output token each; the relevant case returned a valid `give` action and dialogue together in 35 output tokens.
Total usage was 1,584 tokens and latency was 1.03–1.83 seconds. These fixtures use synthetic nearby state and do not
exercise the game scheduler or world actions. The full 20-NPC content review and integrated gameplay remain unverified.

Historical Qwen/StartLux probes and old 3-NPC gameplay probes are not current acceptance tests. Some also capture
screenshots or assume old queue semantics; do not run them blindly. Use the focused current protocol/policy checks and
the user's manual gameplay review.

Existing packaged executables are unchanged until rebuilt and staged. Rebuild the current editor target and a new
package before distributing the cloud-only implementation. Retaining old model files does not mean the new launchers use
them.
