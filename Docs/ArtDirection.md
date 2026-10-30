# Art direction v0.1

Style: warm, sculpted low-detail woodland fable. Rounded readable silhouettes, matte surfaces, moss/sage greens, warm sandstone, amber nutrient glow and muted blue distance. No photoreal textures or ornate armour.

Third-person camera, Windows 1080p target. Player and speaking NPCs use one coherent humanoid family with readable hands, separated limbs and simple short clothing. Scale and colour distinguish roles; no thin dangling parts. Character source target 10k?20k triangles and 1k?2k textures. Generated renders are not proof of animation readiness.

## Required asset families

- 1 shared riggable woodland humanoid base; up to 1 additional silhouette only if necessary after the first import passes. Player + NPC colour/scale variants.
- 1 Mother Tree landmark: use a suitable local source first; otherwise one generated static mesh.
- 1 nutrient plant/fruit family: authored simple geometry or local material-compatible asset; generate only if it materially improves readability.
- 3 rock shapes, 2 canopy shapes, 1 trunk shape, 1 grass clump: author simple procedural meshes or reuse verified coherent local sources. Zero new building models for MVP.
- UI and world-state icons are code-native. No generated text/UI textures.

## Provisional service caps (not spending commitments)

- Tripo: first-round cap 600 / user maximum 3000 credits; coherent animated character candidate.
- V2fun: first-round cap 300 / user maximum 5100 credits; at most two static natural landmarks if required.
- Hyper3D: first-round cap 10 / user maximum 223 credits; character pipeline comparison/fallback. No simultaneous duplicate production batches.

Read-only API balance and permission checks precede generation. Final supplier selection follows local-asset inspection and documentation review. Keep one provider for the character family. Submit one representative asset, inspect it in-engine, then approve further generation. No blind paid POST retries. Track every request, reserved cost, actual charge and refund in the ledger. Keys stay outside the repository.
