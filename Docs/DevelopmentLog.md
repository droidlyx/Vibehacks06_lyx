# Development log

## 2026-10-31 ? independent project bootstrap

- Established a fresh repository over SSH; no earlier project history imported.
- Recorded the user-approved continuous-growth / NPC-world concept and first-playable boundaries.
- Created the independent Verdant UE 5.8 module and build targets.
- Started parallel official API documentation research for Tripo3D, V2fun and Hyper3D.
- Defined an initial coherent art direction and conservative spending caps; no paid generation submitted.
- Existing Ember working files are left untouched.

Validation: repository clone succeeded. Engine compilation and gameplay validation pending.

## Toolchain checkpoint

- First build found the installed UE 5.8.3 requires BuildSettings V7 for shared editor build products. Updated both targets to the installed engine setting.
- Bootstrap commit pushed successfully: 2d8856d.

## API and source-art checkpoint

- Read official Tripo v3, V2fun v1 and Hyper3D API documentation; endpoint, pricing and uncertainty notes are in GenerationAPIs.md.
- Authenticated read-only balance checks succeeded: Tripo 3000, V2fun 5100, Hyper3D 223. Zero paid generation tasks.
- Found CC0 original Quaternius character/animation/nature archives. Preparing a fresh import and animation validation; no derived Ember asset copied.
- Independent VerdantEditor build passed on UE 5.8.3 (BuildSettings V7).
- Corrected README UTF-8 title after the initial PowerShell pipe encoding issue.

## First integrated runtime findings

- Core gameplay module compiles. Fresh map and palette materials generated successfully.
- First real launch exposed an accidental APawn::Restart override in the R-key handler, causing repeated map travel. Renamed the handler to RestartJourney. Rider currently indexes only the reference project, so the attempted semantic rename could not operate on the new project; three direct references were corrected locally.
- Local Qwen service loaded successfully and returned a real Chinese JSON negotiation response in about 0.25 seconds in a simple probe.
- Runtime validation remains in progress; no playable-complete claim yet.

## Original gameplay milestone

- Added an original procedural valley, continuous size/health/reach growth, timed melee, dodge, loot, harvesting and sharing.
- Added three territorial food sources, NPC harvesting/flight/migration, true delivery pledges and Mother Tree restoration.
- Added Chinese HUD, dialogue input, real local model HTTP requests and validated action execution.
- Full editor compilation passes after refreshing the UBT source manifest with -gather.
- Added runtime smoke and world-behaviour probes; gameplay/visual pass is still pending final character import.
- Fixed start-up map reload caused by the APawn lifecycle collision and corrected invalid/contradictory dialogue actions during review.

## Art and live-world verification milestone

- Rebuilt 3 CC0 characters on a shared rig, imported 9 clips and 6 nature meshes from original vendor files. No generated-asset credits spent.
- Fixed Blender hidden bone-display geometry, material slots and source rest-pose differences before final import.
- Rendered game smoke passes 16/16, world delivery/territory tests pass 13/13, actual in-game local AI integration passes 3/3.
- Improved negotiated action schema after retaining and inspecting failed small-model cases; 3 selected protocol cases now pass.
- Captured and inspected stable gameplay/dialogue images. Original teleport-frame image was rejected and replaced.
- Independent Windows packaging underway. Explicitly included runtime-loaded engine basic shapes after inspecting cook output.

## First playable / v0.1

- Packaged Windows executable passed 16 smoke + 13 world-behaviour + 3 real-AI integration checks.
- Verified the staged model hash, local AI runtime, Chinese UI and packaged runtime-generated geometry.
- Added explicit engine basic-shape cook dependencies and disabled unused Android file serving.
- Portable entry point: Builds/Windows/Play.cmd. This folder includes the local model and licenses.
- User subsequently authorized continued autonomous playtesting and fun/quality iterations after this prototype milestone. Preserve this first playable while making the next iteration.

## v0.2 playability checkpoint

- Preserved the v0.1 Windows package and tagged source; developed on iteration/playability-v0.2.
- Fixed strafing attack direction, interrupted ghost attacks, animation/hit-clock disagreement, roll recovery, conversation interruption and growth-camera framing. All 40 explicit combat/input checks passed at 1280x720, including the 4.5m body above the expanded HUD.
- Added contextual legal gathering/tax information, actual delivery progress and carried-food visuals. Held E gathers repeatedly; Shift+F makes a bounded bulk offering.
- Kept real food reserves on wilderness opponents so combat loot remains useful; no synthetic kill reward.
- A normal-resource engine-input journey passed 9 checks in 40.78 simulated seconds: first gathering at 14.61s, height 0.81m to 1.59m, 19 real food looted, player22 + Miya5 actually delivered. No teleports, grants or frozen NPCs were used in this journey. It is not yet a full100-food completion.
- Deterministic 90s policy comparison exposed a genuine tree-canopy navigation blockage. Rider/LLDB captured the actual collider and stopped branch; replaced foliage collision with trunk capsules. After the fix, opening highland changed a resident route and caused30 actual non-owner food harvests versus0 in the closed control, followed by resource-driven migration. Failed baseline retained; see NavigationDebug.md.
- Narrowed paths, added nearby natural scale references and reduced haze. Added six reproducible original mono48kHz SFX, imported and ran the journey with the audio device enabled. Waveform checks pass; physical listening has not been verified. Paid generation spend remains0.
- Native desktop-control helper was unavailable after retry/reinitialization. Validation uses engine-input replay, not physical desktop automation or a human fun rating.

Next: verify normal-resource completion, make the resulting world state easier to compare, then package separately from v0.1.

## v0.2 world-outcome and full-route checkpoint

- Added an immutable restoration-season record with the opening/restoration population, per-grove control/access/tax and actual per-person donation ledger. A later death or policy change affects the live world without rewriting the earlier record. Fully free/open groves are now distinct from the initial divided/taxed state.
- Reviewed and fixed three concrete boundaries: a legal0.01-food pledge no longer stalls; gifts earn trust proportional to the actual transfer and cap gift-only goodwill at12; dialogue requests, execution and asynchronous replies revalidate physical conversation distance. Leaving range also releases the dialogue UI/input.
- All25 world-rule checks passed, including the new fractional-transfer, distance, accounting and season-snapshot cases.
- A complete normal-resource engine-input route restored100 food at about101s and exited at104s. Contributions were player55, Miya5, Lanshi20 and Taoen20. Four additional real-model requests received replies; two actual policies became free access. The resulting state was3 living main residents,1 living wilderness NPC and3 free groves. Logs show Lanshi independently defeated the red-backed raider during the route. This is an observed run, not a claim that every model response or route has the same result.
- The same route travelled396m, without teleporting, granting resources/size or freezing residents. The photographed end-state UI was readable at1280x720. The next capture route walks back physically for a wider view and reports restoration time separately from capture time.
- A brief early-scene Unreal CSV profile at1600x900 on RTX5060 Laptop measured mean frame5.15ms / GPU5.14ms, game2.01ms, draw3.44ms. The initial apparent low-FPS concern was a log-counter wrap, not a measured rendering bottleneck. No speculative graphics downgrade was made. A native-package sample follows.
- Build tooling now archives each version separately and stages model/visual licenses. It does not switch the launcher until validation succeeds. Source provenance refresh preserves audio and nonzero spend fields from other asset producers.

- Fixed the2.49 growth-stop boundary: the player can reach2.5, digest remaining nutrition, and spend food on healing at maximum size. Full-size/full-health eating preserves inventory. All45 combat/input checks and16 core smoke checks pass after this change.

## v0.2 native-package verification

- Versioned Windows BuildCookRun succeeded.100 native checks passed, including a full-resource route restoring at99.75s; all five ordinary validation processes exited0.
- Retained a native boot-CSV shutdown failure (777003), attempted native debugging, documented the unbound engine-source breakpoint, and performed matched option comparisons. CSV-only and CSV+GPU-stat smoke failed on exit; GPU-stats-only and ordinary full journeys exited0. No engine/crash handler was patched. Formal gameplay tests are independent of optional instrumentation.
- Re-tested portable AI from a cold start using its copied runtime, model and DLLs; real native dialogue passed.
- Fixed the PowerShell5 report-merging array wrapper and verified five latest actual case records survive JSON round-trip, with earlier failed cases retained separately.
- Verified the original v0.1 executable hash is unchanged; prepared v0.2 launcher promotion and packaged controls/licenses/demo instructions.

## v0.3 first integration

- Started the user-authorized two-hour hidden iteration at2026-10-31 05:47:49 Asia/Shanghai; no game/editor window was shown.
- Added distinct food reserves, matching donations from real public help, net-gift/spent-credit accounting, follow willingness and peaceful transfer of a single controlled grove. Requests and execution share authoritative limits.
- Added paid B-key grove tending and player policy cycling (free/taxed/closed), preserving actual food and state changes.
- Companions now intervene against threats, avoid their party, preserve agreements when attacked by outsiders, retreat/heal from real food and retry blocked travel without remote fulfilment.
- Added one-shot late-click attack buffering, cancelled by stagger/dialogue/dodge, and targeted input checks.
- First editor integration compiles. Social/world/NPC/combat/real-AI probes all exited0. These are fixtures/integration cases, not yet the v0.3 normal-resource full route.
- Render review found Canvas text missing despite valid Slate text: a file-backed Slate font has no UFont object required by this engine's Canvas path. Also found over-wrapped footer context. Both are explicitly pending the next visual fix; not claimed visually complete.

## v0.3 second integration / feature freeze

- Fixed Canvas labels with a GC-held runtime UFont, widened footer text slots, and verified actual Chinese labels/height ruler/damage arcs in hidden720p captures. Offset overlapping damage numbers and omit place labels over the player or corner HUD.
- Separated per-NPC transcripts/status from the in-flight RequestNPC; real-model callback-switch tests pass. Explicitly provided production-validated legal actions to the model after retaining a case that confused blocked cede with close; the tightened real-model refusal check now passes.
- Added complete-exchange bounded memory and rejected cyclic follow chains. NPCs now use their leader's area when escorted away from their birthplace, avoid endless attacks through walls and can perform bounded, paid soil care.
- Improved the validator to require a fresh report with zero reported failures as well as a zero process exit. A first steward route reported a failed ownership assertion despite editor exit0; it was retained, then the route earned more real help instead of weakening the game rule.
- Complete public and peaceful-steward routes now both pass, preserving all3 main residents and creating distinct ownership/access outcomes.
- Closed/open600-second natural simulations each checked36007 frames, with no invariant failures or reported stationary-travel suspects. Both eventually settle with sufficient stock; this does not demonstrate endlessly escalating autonomous stories.
- No generated models or paid API tasks were added. Preparing a separate native v0.3 package; v0.2 remains intact.

## v0.3 native validation and editor handoff

All eight hidden native cases completed successfully at 2026-10-31 07:14 Shanghai. Final handoff resumed after an interrupted session; no claim of continuous work during the interruption. Gameplay source: 2d7ba41. Stable v0.2 preserved. Added reproducible independent editor-project export, including source, uncooked content, configuration, editor module and local AI; excluded old build output, git history and caches.
