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
