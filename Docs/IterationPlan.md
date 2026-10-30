# v0.2 iteration: readable choices and responsive actions

Stable baseline: prototype-v0.1 / dc0fbca. Its packaged folder remains unchanged at Builds/Windows.

## Evidence from v0.1

- 32 rendered/packaged fixture checks passed. They prove mechanics, not an unassisted full playthrough.
- Default attack direction could change during strafing; taking a hit could replay an attack animation without resetting its clock.
- Roll animation was imported but unused; the short repeat interval allowed high invulnerability uptime.
- The enlarged player could be cropped by the bottom HUD.
- A 100-food restoration could require 20 separate offering taps; contextual permission and share information was mostly in the global panel.
- Frequent NPC harvest messages obscured consequential events. A negotiated delivery was hard to follow after closing dialogue.
- Source-level economy estimate: growth to about 1.4m costs approximately 15 nutrition, but the weakest wildlife often retained only about4 food. This is a tuning hypothesis, not an observed human playtest.

## First iteration

1. Fix attack/roll/hit timing and direction, conversation interruption, and camera framing.
2. Show nearby interaction eligibility and the actual last negotiated delivery; render carried nutrition.
3. Hold E for repeated harvesting, Shift+F for an explicitly contextual bulk offering.
4. Try actual wildlife reserves (16/24/36) and starting inventories (22/34/48), without spawning extra death rewards.
5. Try asymmetric grove supply (42/8/38, fertility1/.35/1) so access policy can change real routes.
6. Narrow the visual road, add a few nonblocking nearby scale references, reduce uniform yellow haze. Generate zero new assets.

## Evaluation

- A normal-resource engine-input replay walks, gathers, eats, negotiates, fights and contributes. It must not teleport, freeze NPCs, grant food or set body size.
- Separate combat fixtures test interrupted attacks, dodge windows, movement-facing competition and maximum-body framing.
- Two90-second natural-world recordings differ only by opening the highland at about2 seconds. Compare real positions, legal access and the actual harvest ledger; do not assert a desired story without evidence.
- Windows desktop control was attempted, retried and reinitialized but its native pipe was unavailable. Engine-input replay is explicitly not physical desktop input or a claim of human playtesting.
- Rebuild into a versioned folder and keep v0.1 playable until the next package passes.
