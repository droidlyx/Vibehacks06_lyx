# Validation record

## Engine/runtime

- Unreal Engine 5.8.3, Windows, Development configuration.
- Independent VerdantEditor compilation passes.
- Actual rendered game smoke: 16 checks passed (terrain collision, ground contact, continuous growth, size-related strength, timed melee hit, conserving food transfers, access restrictions and restoration/state-dependent outcome).
- Actual world-behaviour probe: 13 checks passed (negotiated opening, physical delivery, no instant remote donation, death interruption, one-time loot, release of territory).
- Actual in-game local AI probe: 3 checks passed; a real Chinese reply was displayed and interpreted while resource totals remained consistent. NPC proposed personally delivering food rather than blindly granting the player's request.
- Production protocol probe: 3 selected real-model negotiation cases passed after prompt/schema refinement. Earlier failures were retained locally. These are finite examples, not a guarantee of every conversation.
- Updated 1600x900 start, restored-world and dialogue screenshots inspected. Character material/pose is readable; model and nature assets render. Motion blur disabled, captures deferred after teleports.

## Reproduction

Run the game executable or UnrealEditor game mode with `-VerdantSmoke`, `-VerdantWorldProbe` or `-VerdantAIProbe`. The AI probe requires the local server from Start-Game.ps1. Each test writes its result to the running project's Saved/Validation directory and exits nonzero on a failed check. A packaged-build rerun is recorded below when complete.

## Limits

This is a playable prototype, not exhaustive QA. Animation timing across all movement directions, long-lived social equilibria, every free-text request, and other hardware have not been exhaustively verified. The dialogue executor rejects invalid actions and never substitutes canned dialogue for a failed model call.

## Packaged build

Windows Development BuildCookRun completed successfully. The actual packaged executable (not UnrealEditor) passed all 16 smoke, 13 world-behaviour and 3 real-AI integration checks, with zero failures. Packaged 1600x900 screenshots confirm Chinese text, character materials and runtime terrain/plant geometry render correctly.

The portable folder includes the verified Qwen model, llama.cpp binaries/licenses and Play.cmd. The copied model SHA-256 matches official metadata. This verifies the build on this workstation; it is not a claim of testing every GPU or clean Windows installation.

## v0.2 independent Windows build

Built from gameplay commit fc601b7. The native executable passed100 checks:16 core smoke,25 world rules,45 combat/growth/camera/restart,3 real AI integration and11 normal-resource full-journey checks. All five formal process exits were0.

The final full journey restored the tree at99.75s, with no teleports, size/resource grants or frozen NPCs. It physically travelled409.2m, made four additional real-model requests, changed access policies and completed100 actual food contributions. Exact final actors, resources, dialogue and donations are in Checks/v02-native/full-journey.json. These known-route replays do not estimate a first-time human player's completion time.

1280x720 framing at0.45/1.4/2.5 body scales and the expanded HUD passed. The maximum-size nutrition boundary, fractional gifting, minimum legal delivery, distance revalidation, immutable season history and non-duplicating donation ledger were exercised. Native screenshots were inspected at720p and900p.

The portable package's own llama-server and copied model were cold-started after stopping only the earlier task-owned source service; the native AI test passed again. The model hash matches its recorded source. Physical listening/mix judgement, human fun judgement, gamepad support and clean installations on other PCs remain unverified.

Native boot CSV capture reproducibly triggers a shutdown exit777003 on this installed toolchain. It is separate from normal play; ordinary full-journey controls exit0. This was not silently counted as passing or suppressed by disabling crash handling. See [NativeProfilingExit.md](NativeProfilingExit.md) for evidence and the unresolved engine-symbol limitation.

The stable v0.1 executable hash remains unchanged. Versions are archived separately, and the active launcher is promoted only after formal checks.
