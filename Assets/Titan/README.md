# Selected Project Titan creatures

Copied on 2026-10-31 from the local `E:/UE_projects/ProjectTitan` Unreal Engine 5.8 sample at the user's request.
Original `/Game` package paths are preserved. The source project is unchanged.

| Selected appearance | Purpose                                                              | Mesh package                                                             | Available animation packages                                                            |
|---------------------|----------------------------------------------------------------------|--------------------------------------------------------------------------|-----------------------------------------------------------------------------------------|
| Jellybean cat       | Rounded small woodland NPC; green, blue and yellow material variants | `/Game/Characters/NPCs/Clifftop/Creatures/Jellybean_Cat/SKM_Jellybean`   | `Animations/Jellybean_Idle`, `Animations/Jellybean_Walk` under the same folder          |
| Small penguin       | Friendly round bird NPC                                              | `/Game/Characters/NPCs/Arctic/Penguin/Rigged/Small/T-Pose_weight_color`  | `Idle`, `AS_Penguin_Small_Walk`, `Happy_Idle`, `Jumping` in the same folder             |
| Desert ferret       | Large-eared quadruped NPC                                            | `/Game/Characters/NPCs/Desert/Creatures/Desert_Ferret/SKM_DesertFerrret` | `/Game/Characters/NPCs/MarshlandType/Kelpie/Animations/AS_Kelpie_{Idle,Walk,Run,Sleep}` |

The Jellybean mesh names `SK_Jellybean` as its skeleton, while its supplied clips name `SK_QuadrupedBase`. Compatibility
must be verified in Unreal before using those clips. The ferret and Kelpie clips name the same `SK_QuadrupedBase`; the
small penguin mesh and clips name the same small penguin skeleton. Matching skeleton references alone do not prove
satisfactory animation appearance.

`manifest.json` records the conservative dependency closure: 90 package files, approximately 213.42 MiB, with SHA-256
hashes verified after copying. Shared material dependencies include landscape functions and virtual textures. The
closure also includes the source skeleton's available preview references; this is why some unselected assets, such as
the sulfur ferret, are present. Titan actor/animation Blueprints were not selected for integration.

The raw reference scan found four absent character package paths in the source quadruped skeleton (Fred, Kai Deer,
Kelpie, Molted Lizard). Preserve this fact when assessing load warnings; do not treat these as lost migration files. The
fifth reported path, `/Game/Landscape/Materials/Textures`, is an import directory in `T_GlobalBlend_M`'s Interchange
metadata, not an asset package. Unreal inspection must distinguish editor references from runtime requirements.

`Previews/*.jpg` are existing embedded asset thumbnails extracted directly from source packages. They are not new editor
screenshots or evidence of in-game appearance. `candidates.jpg` includes rejected comparison candidates: Bunkin and
Frogward have humanoid proportions; Biscuit lacks a locomotion clip in its local folder. Kelpie's local preview is a rig
placeholder, so its mesh was not chosen.

Re-run `python Tools/migrate_titan_creatures.py` for a read-only closure/conflict check. Add `--apply` to copy missing
files; the script refuses different files at an existing destination path. It does not overwrite project assets.

These assets retain the applicable Epic / Project Titan terms; no CC0 license is claimed. No Titan assets were uploaded
to any external model generation service. The source asset provenance and project terms should accompany any distributed
build.

## Verdant runtime adaptation

The runtime uses the penguin original Idle/AS_Penguin_Small_Walk, plus four locally baked mesh-bind-pose loops at `/Game/Art/Creatures/Animations/A_{Jellybean,Ferret}_{Idle,Walk}`. See `motion-report.json` and `Tools/prepare_titan_motion.py`. Original Jellybean and Kelpie clips are retained as source references, not used by runtime. Cat Body is matte role colour; missing ferret Eyes material is supplied at runtime. M_Verdant now supports skeletal meshes. All chosen meshes face +Y and are rotated -90 degrees to gameplay +X. Ferret reference height is 110 cm and uses torso/root horizontal anchoring to keep its long tail from shifting collision placement. The original imported files remain unchanged. Native Editor build passed; no automated PIE or screenshot capture was performed, and visual/gameplay review is pending.
