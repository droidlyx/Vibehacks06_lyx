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
