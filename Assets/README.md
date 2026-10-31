# Prototype art intake

The current character redesign uses an original procedural seed/sprout hero and selected cartoon creatures from the
local Project Titan project, alongside the existing Quaternius nature assets and original synthesized sounds. The
previous humanoid assets are retained as an archive and are unused by the current runtime character presentation. No
Ember map, material graph, gameplay source, Blueprint, or prepared Unreal asset was copied.

## Current creature cast — 2026-10-31

| Role            | Current appearance                                                                                                                         |
|-----------------|--------------------------------------------------------------------------------------------------------------------------------------------|
| Hero            | Original procedural seed/sprout sphere creature; compress-and-release bump/slam attacks timed to the existing 0.60 normalized damage point |
| NPC agents 1, 3 | Titan small penguin                                                                                                                        |
| NPC agents 2, 5 | Titan Jellybean cat with matte presentation                                                                                                |
| NPC agents 4, 6 | Titan desert ferret                                                                                                                        |

The selected Titan migration contains 90 package files, approximately 213 MiB including
dependencies. [Titan/manifest.json](Titan/manifest.json) records copied paths and SHA-256
hashes; [Titan/README.md](Titan/README.md) records source, available animations and compatibility findings. Source files
were not modified or uploaded to external services. Titan assets retain the applicable Epic / Project Titan terms and
are **not CC0**.

Four simple idle/walk clips have been baked against the cat and desert ferret meshes' own bind poses. Jellybean's supplied clips reference a different skeleton and are not used at runtime. The native Development Editor build passed;
user-run gameplay/visual review remains pending. No new visual acceptance or gameplay test pass is claimed. This redesign spent no external generation credits.

## Previous humanoid preparation and retained nature sources

- Quaternius Modular Character Outfits – Fantasy, Standard: Female Ranger (traveler), Male Peasant (gatherer), Female
  Peasant (keeper).
- Quaternius Universal Base Characters, Standard: head/neck geometry from the matching base bodies, with two optional
  hair meshes.
- Quaternius Universal Animation Library 1 and 2, Standard: idle, walk, jog, punch, hit, death, roll, consume and carry.
- Quaternius Stylized Nature MegaKit, Standard: CommonTree_1, TwistedTree_1, Rock_Medium_1/2/3 and Grass_Common_Short.

Original archive licenses are retained under `Source/Quaternius`. `provenance.json` records source URLs, archive hashes,
selected file hashes and original member names. These specific acquired Standard archives explicitly contain CC0-1.0
licenses. This is not a blanket license claim for all products from the same author or for the separately migrated Titan
content. No AzielArts or paid sword-animation pack is included.

## Historical humanoid preparation

The preparation, import and screenshot observations below describe the previous humanoid build, not validation of the
new creature presentation.

Run `python Tools/prepare_local_assets.py` from a machine with the documented local source library. The script reads
original vendor ZIPs, extracts only selected files and their dependencies, and invokes a fresh Blender 4.5 background
process for each character. The original library stays unchanged.

Clothing and base heads have the same 65 joint names/parents as UAL, but their rest transforms differ. The preparation
script newly deforms each source mesh into the common UAL rest pose, retains skin weights, combines head/clothing and
exports all three on the same rig. It exports each selected original UAL clip separately. The upstream base-character
ZIP contains an eye-normal filename typo; staging copies the existing original texture under its referenced alias. This
is visible in the provenance member/path mapping.

`Prepared/characters_preview.png` is an intermediate Blender inspection render, not evidence of Unreal runtime
validation. Observed finger deformation was fixed by updating the pose hierarchy parent-first while baking. Blender
imports a hidden 42-vertex Icosphere for bone display inside `glTF_not_exported`; that collection is explicitly excluded
from asset geometry. Export context is explicit and every mesh FBX is checked for Geometry records. Hair is tinted warm
brown. Final headless Unreal import succeeded for all 3 characters, 9 clips and 6 nature meshes, with clean character
heights around 1.8 meters. The first actual-game start screenshot shows the Traveler with correct textured clothing and
a normal walking pose, without missing-material checkerboard or stretched limbs. Full animation timing and facing
relative to movement still require live playback; a single screenshot is insufficient.

## Unreal import

After compiling the project's editor module:

```
UnrealEditor-Cmd.exe Verdant.uproject -run=pythonscript -script=Tools/import_assets.py -unattended -nop4 -NullRHI -ExecCmds="Interchange.FeatureFlags.Import.FBX 0"
```

The script requests the native FBX factory (existing asset reimports may be handled by Interchange) and reconstructs a
small set of matte materials from the original base-color textures. Masked leaf/grass materials retain alpha and are
two-sided. High-frequency normal detail is deliberately omitted for the prototype's soft natural palette.

Retained historical character references and active nature references:

- `/Game/Art/Characters/SK_Traveler`
- `/Game/Art/Characters/SK_Gatherer`
- `/Game/Art/Characters/SK_Keeper`
- `/Game/Art/Characters/Animations/A_Idle`, `A_Walk`, `A_Run`, `A_Attack`, `A_Hit`, `A_Death`, `A_Dodge`, `A_Consume`,
  `A_Carry`
- `/Game/Art/Nature/SM_Tree01`, `SM_Tree02`, `SM_Rock01`, `SM_Rock02`, `SM_Rock03`, `SM_Grass01`

`Prepared/unreal_import_report.json` records actual imports and durations. Nature instances should be sized from mesh
bounds, rather than assuming authored scale. Gameplay collision uses simple proxies; visual meshes are not the
authoritative character shape.

No paid generation calls were made for this set. The three generated-asset service budgets remain untouched by this
asset pipeline.

## Runtime inventory and budget

`manifest.json` records the earlier asset intake: 3 humanoid meshes, 9 humanoid animation clips, 6 nature meshes, the
original procedural Mother Tree, one original procedural nutrient-plant family, 6 original short SoundWave effects, and
the shared SC_VerdantSFX sound class. The current creature replacement is recorded separately in `Titan/manifest.json`;
the retained humanoid entries do not mean those characters are still selected at runtime. The Mother Tree and nutrient
plants are assembled at runtime from engine primitives and project-authored parameters; no generated 3D models are
required. Different body sizes reuse the same source meshes.

Planned external 3D generation count is now **zero**. The read-only official balance snapshot in
`generation-ledger.json` records Tripo **3000**, V2fun **5100**, and Hyper3D **223** credits. This project's art
pipeline made no generation requests and spent **0** credits on each service. These are recorded snapshot balances, not
a claim that unrelated account activity cannot change them.

Historical humanoid-build validation: the first gameplay smoke test passed 16 checks; the world-interaction probe passed
13, both with zero failures. These results do not validate the creature redesign. `Saved/Validation/valley-start.png`
supports the former Traveler's initial material/silhouette acceptance. The first restored-world capture was rejected
because it was taken on a teleport frame inside foliage. A later historical capture after camera settling and with
motion blur disabled shows the restored tree and enlarged Traveler; no batch reimport was needed at that time.

## Original synthesized sound effects

The six `Assets/Audio/AFX_*.wav` files are original project audio generated by `Tools/generate_sfx.py` from
deterministic oscillators, envelopes and seeded noise. They use no third-party recordings, music, external generation
API or paid credits. Their hashes and measured peak/RMS/duration are recorded in `Assets/Audio/manifest.json` and the
main provenance list.

`Tools/import_sfx.py` has now run successfully in Unreal: all six mono 48 kHz SoundWaves were imported into
`/Game/Art/Audio` and assigned to `SC_VerdantSFX`. The shared class gain was read back as approximately 0.630957 (-4
dB). See `Assets/Audio/unreal_import_report.json` for the actual imported formats, durations, hashes and class
assignments.

These facts establish technical preparation and import only. No listening approval is recorded; headphone/speaker
audition and overlapping-event gameplay balance remain pending.

The CC0 licenses retained for Quaternius inputs apply to those listed visual/animation sources. They do not license the
separately authored project code or synthesized audio. No top-level project reuse license is currently present; see
`Assets/Audio/README.md` for the audio origin and license-status note.
