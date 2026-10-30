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
