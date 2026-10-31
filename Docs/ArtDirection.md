# Art direction — creature redesign, 2026-10-31

Style: warm, sculpted low-detail woodland fable. Rounded readable silhouettes, matte surfaces, moss/sage greens, warm
sandstone, amber nutrient glow and muted blue distance. No photoreal textures or ornate armour.

The current environment target is a visible mountain sunrise: a distinct solar disk and localized amber dawn band
against a cool sky, aligned with direct sunlight above the arrival ridge. It retains the smoke-free adaptation of
Titan's clouds and the existing DX11 renderer. Settings are saved in Valley; visual and frame-rate acceptance await
manual Play.
See [EnvironmentLook.md](EnvironmentLook.md).

Third-person camera, Windows 1080p target. The hero is an original procedural seed/sprout creature built around a simple
sphere silhouette, with compress-and-release motion. Current source selects 20 local Project Titan character types from
the 53-type design catalog, extending the earlier three-species creature pass. Matte materials, restrained colour and
readable silhouettes remain the direction; integration does not establish visual or animation acceptance. Geometry and
texture budgets must be measured per selected asset rather than inferred from the old humanoid targets.

## Required asset families

- Hero: original procedural seed/sprout creature; simple bump/slam attacks align the visible impact with the existing
  0.60 normalized damage point.
- Current NPC selection and model/clip paths are recorded in `Source/Verdant/VerdantPopulationProfiles.inl` and
  `Tools/stage_population_assets.py`. These 20 types are a subset of the design catalog, not all 53 candidates.
- Verify clips against each selected rig. The earlier three-species pass retained penguin, Jellybean and ferret assets
  and four baked creature loops; its Jellybean skeleton compatibility findings remain in `Assets/Titan/README.md`.
  Those findings do not establish animation acceptance for the expanded population.
- 1 Mother Tree landmark: use a suitable local source first; otherwise one generated static mesh.
- 1 nutrient plant/fruit family: authored simple geometry or local material-compatible asset; generate only if it
  materially improves readability.
- 3 rock shapes, 2 canopy shapes, 1 trunk shape, 1 grass clump: author simple procedural meshes or reuse verified
  coherent local sources. Zero new building models for MVP.
- UI and world-state icons are code-native. No generated text/UI textures.

The initial three-species Titan migration contains 90 package files (approximately 213 MiB), including dependencies,
with source paths and
hashes in [Assets/Titan/manifest.json](../Assets/Titan/manifest.json). The source project is unchanged. Titan content
retains its applicable Epic / Project Titan terms; the Quaternius CC0 terms do not cover it. No external generation
spending was needed for this redesign.

Historical validation: the three-species implementation and four per-rig clips were saved; its native Development
Editor build passed on 2026-10-31 and the editor was reopened. This does not validate the expanded population or the
current integrated changes. Manual gameplay/visual review remains pending. Earlier v0.3 screenshots and test results
describe the previous humanoid build. Gameplay testing is reserved for the user; no new visual acceptance is claimed.

## Provisional service caps (not spending commitments)

- Tripo: first-round cap 600 / user maximum 3000 credits; coherent animated character candidate.
- V2fun: first-round cap 300 / user maximum 5100 credits; at most two static natural landmarks if required.
- Hyper3D: first-round cap 10 / user maximum 223 credits; character pipeline comparison/fallback. No simultaneous
  duplicate production batches.

Read-only API balance and permission checks precede generation. Final supplier selection follows local-asset inspection
and documentation review. Keep one provider for the character family. Submit one representative asset, inspect it
in-engine, then approve further generation. No blind paid POST retries. Track every request, reserved cost, actual
charge and refund in the ledger. Keys stay outside the repository.

In the earlier three-species presentation, the long-tailed ferret uses a 110 cm reference height and a matching 55 cm
capsule half-height per size unit; its visual horizontal anchor is the rig origin near the torso, not the tail-inclusive
bounds centre. Other creatures in that pass and the hero use the 180 cm reference height. These are not universal size
assumptions for every model in the expanded population.

## Environment scale and ecology — applied 2026-10-31

Twelve active nature meshes follow four stable world-space scale bands. Each repeated
mesh keeps its own nominal height with continuous uniform 0.8–1.2 instance scaling:

- Ground: grass 25 cm, clover 8 cm, flowers 28 cm, true pebbles 12/14 cm.
- Understory: bush 1.1 m, fern 0.7 m, medium rocks 1.2/1.5/1.8 m.
- Ordinary woodland: CommonTree 6.5 m.
- Upper canopy: TwistedTree 14 m; the unique existing Mother Tree remains approximately 22 m.

The full 68-mesh allocation is in `Assets/Environment/scale-plan.json`, distinguishing
active and reserve meshes. Six CC0 meshes were imported from the existing local Quaternius
pack. Source hashes and material descriptions are in `ecology-assets.json`; current
terrain height generation and character scale rules are not part of this ecology edit.

`VerdantEcology.cpp` replaces uniform scatter and repeated roadside rows with ten irregular
forest regions, sparse high-canopy accents, rock groups and anchor-adjacent understory.
Separate seeded streams keep layers reproducible. Clearances preserve routes, grove
interaction spaces and the Mother Tree approach. Steep/unsupported placements are
rejected; actual counts are logged at the user's next Play.

The smallest scale has a persistent population of short grass and clover across eligible
near-player terrain, not just isolated decorative patches. `VerdantGroundCoverComponent`
keeps 25 reusable six-metre world cells with jittered sites and mild density variation.
Positions and sizes remain the same after cell unload/reload. Fine-detail render radius
is approximately 12 m; farther landscape retains separate decorative islands. It samples
existing terrain triangles instead of evaluating fractal terrain per blade. This foliage
has no collision or dynamic shadow; cliffs above 55 degrees are excluded. Initial cells
fill nearest-first over roughly 2.6 seconds. Coverage and performance await manual review.

The twelve nutrition plants per grove occupy unequal 5/4/3 patches rather than an even
ring, with composite nominal height 1.2 m and continuous +/-20% variation. Gameplay food
accounting is unchanged by this visual placement edit.

Six mesh/material imports were verified and existing nature materials enabled for
instancing. The cold Editor build succeeded. No PIE, gameplay suite or screenshot capture
was run. Live Interchange texture import did crash; isolated native-factory commandlet
import subsequently succeeded. Evidence is recorded in `Docs/EditorScreenshotCrash.md`.
The editor was reopened for user review.
