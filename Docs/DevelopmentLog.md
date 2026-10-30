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
