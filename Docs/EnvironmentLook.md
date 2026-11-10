# Clear-after-rain woodland environment

## Current target: visible mountain sunrise and direct sunlight

The user requested a clear sunrise, visible sun and direct sunlight. The previous
azimuth 48/elevation 8 setting was below a terrain horizon of roughly 36 degrees
from the initial player position. Editor-only `Verdant.InspectSunrise` now samples
the actual HeightAt terrain without Play, reporting both approximate camera and
player-eye horizons in `Saved/Validation/sunrise-horizon.json`.

The current sun is azimuth 96/elevation 25 degrees, just above a roughly 22.6-degree
ridge towards the front-right at arrival. Its sky disk and real directional light
are numerically aligned. A clearer disk and narrow amber dawn band sit against the
cool sky. Directional light is 12, sky fill .60; dynamic shadows are enabled.
Light Shaft Bloom (.15) adds view-dependent radial sunlight around the solar disk;
it is not a world-space volumetric beam. Global volumetric fog remains disabled.

Map/material settings and native authoring utility compilation passed inspection.
The horizon calculation excludes trees, props, clouds and triangle-interpolation
differences, and uses an approximate starting camera. Visual acceptance still
belongs to manual Play; look slightly upward and right of the initial heading.

## Previous target: quiet morning for Mosslit Paths

The user found the blue/ivory concept palette too sunny for the music. The current
sky uses muted slate blue and a pale blue-green horizon, pearl clouds and a small
localized warm sunrise. Solar disk/halo brightness and saturation were reduced;
the disk edge is diffuse. Sun intensity is 13 instead of 21. Sky fill (.65),
exposure bias (.35), thin fog, solar direction and cloud geometry are unchanged.
No fog was added and no gameplay/layout/audio edits were made. The music artwork
and its documented quiet chamber-folk direction informed this pass; no claim of
an audio audition is made. `Assets/TitanSky/dawn-report.json` records the saved
settings and backup. Visual acceptance remains pending user review.

## Previous palette: Verdant concept art

The user accepted the sky structure and requested project colours. The current
palette follows `valley-art-target-v01.png` and `fractal-world-v01.png`: clear light
blue upper sky, muted blue-grey distance, ivory clouds, and localized cream/honey
sunlight. Pink/purple sunset colouring was removed. Only sky shader RGB, cloud
Albedo RGB and directional-light RGB changed; sun position/intensity, cloud shape,
exposure and the rest of the scene were preserved. The map/materials are saved;
`Assets/TitanSky/palette-report.json` records the change and backup.

## Accepted sky structure: B-612 sunset reference

The user narrowed the scope to matching the reference sky: a low golden sun,
peach/pink clouds and a blue-violet upper sky. `Tools/setup_sunset_sky.py` applies
this override after the clear baseline. A custom opaque Unlit/IsSky sphere sets
the horizon/zenith palette and solar disk, behind the existing volumetric clouds.
The 200 km radius avoids clipping their horizon tracing. Height-fog cutoff keeps
the distant dome clear without changing the near-ground fog.

The solar disk is at azimuth 48 degrees/elevation 8 degrees, aligned numerically
with the negative directional-light forward vector. Python Rotator arguments are
named explicitly: its positional ordering differs from C++ and caused an earlier
orientation mismatch. Matching sun and sky fill are updated; ground geometry,
materials, foliage, gameplay and global post-processing are unchanged by this pass.
Shader compilation and saved properties are verified. Visual matching remains
pending user review. `Assets/TitanSky/sunset-report.json` records the backup and
settings; the sections below describe earlier environment iterations.

## v2 correction after user visual feedback

The first pass was rejected as gray and apparently unlit. Its Mie scattering
coefficient was incorrectly set to 0.55 per km, about 138 times UE's default
0.003996. The saved v2 profile corrects it to 0.0025, absorption to 0.0003 and
aerial-perspective scale to 1.0. Height fog is now 0.0015, starts at 40 m and is
capped at 25% opacity. Secondary/volumetric fog, cloud shadowing and cloud ambient
occlusion are disabled for a clear baseline. Sun strength and exposure are unchanged.
The original first-pass description below is historical where these settings differ.
This corrects the known coefficient error; visual acceptance is still pending.

The first environment pass pairs an azure atmosphere with warm side lighting,
cream cloud albedo, cool valley haze and restrained colour grading. It uses the
existing DX11/SM5 renderer. Terrain, player scale, NPC behaviour and camera controls
are not part of this edit.

## Components

- One SkyAtmosphere and one warm directional sun at elevation 32 degrees.
- One movable SkyLight captures the same sky/clouds that appear in the background.
- Titan's soft volumetric-cloud family supplies the source; a separate project
  material removes the volcanic smoke contribution. Titan originals are retained.
- A low-density height fog with a lower secondary layer and a restrained volumetric
  contribution leaves the near play area readable.
- One unbound post-process volume holds fixed exposure, mild saturation/contrast,
  small bloom and screen-space ambient occlusion. No automatic exposure pumping,
  depth-of-field blur or motion blur is introduced.

This pass is a dynamic sky, not a purchased or AI-generated HDR panorama. Real
mountains and trees remain geometry so player growth does not expose a painted
near-ground backdrop. No paid generation is used.

## Reproduction and revision

1. Run `python Tools/migrate_titan_sky.py --apply` for the six local Titan packages.
2. With Valley open and Play stopped, run `Tools/setup_fable_environment.py` inside
   Unreal Python. Save unrelated map edits before running it.
3. The script updates only Verdant-tagged environment actors, creates its own cloud
   material/instance under `/Game/Art/Environment`, and saves the map.
4. `Tools/create_level.py` invokes this same setup when regenerating the launch map.

The source hashes and rights note are in `Assets/TitanSky/manifest.json`. The
environment report records actual saved settings and the backup path. Backups live
under `Saved/EnvironmentBackup`; close the editor before copying an old `Valley.umap`
back to `Content/Maps/Valley.umap`.

## Validation boundary

Property and material-graph inspection is used for this pass. No Rider viewport or
editor-window screenshot capture is allowed, and no automated Play session is run.
The user's next Play should assess cloud coverage, overall brightness, character
readability, distant haze and frame rate. This is a first art pass, not a claim of
completed visual or performance acceptance.
