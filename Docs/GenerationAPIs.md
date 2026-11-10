# Generation API review and spending controls

Reviewed: 2026-10-31. This document records a read-only review of the official Tripo, V2fun and Hyper3D documentation.
No generation jobs were created during research and no credentials are recorded here. Current account balances and API
entitlements still require the local read-only check.

This is the implementation reference for the prototype's required generation, rigging, animation, processing, download
and accounting workflows. Optional endpoints are recorded separately; undocumented capabilities are not treated as
production dependencies.

## Production decision and budgets

Follow [ArtDirection.md](ArtDirection.md). The current cast uses selected local Titan creatures and an original simple
seed/sprout hero; the former shared-humanoid generation plan is superseded. No paid generation is required for this
pass.
If a specific creature or animation gap remains after manual review, validate one complete sample first. Ordinary rocks,
canopies, trunks and grass use simple geometry or verified local sources. The MVP needs no new building models.

Use one supplier and one pinned model family for all related characters. Validate one complete character, including
animation and UE import, before submitting a production batch. Do not generate duplicate character families across all
three vendors.

| Service | User-provided maximum | First-round cap | Intended use                                                                                |
|---------|----------------------:|----------------:|---------------------------------------------------------------------------------------------|
| Tripo   |         3,000 credits |     600 credits | Coherent animated character family candidate                                                |
| V2fun   |         5,100 credits |     300 credits | At most two static natural landmarks if needed                                              |
| Hyper3D |           223 credits |      10 credits | One character-pipeline comparison or fallback, then same-family production only if selected |

These caps are spending limits, not commitments to spend. Credits from different vendors are not comparable units and
must not be added into a single balance. If account balance is lower, the lower amount is the effective limit. Keep a
ledger with provider, asset, model, task ID, sanitized parameters, estimated/reserved cost, actual charge, terminal
status and verified refunds. Never put API keys or signed download URLs in public development logs.

Before every paid submission:

1. Confirm a concrete asset is still needed.
2. Check available API balance and any API-plan restrictions.
3. Reserve the full worst-case known cost within the first-round cap.
4. Submit once and persist the returned task identifier immediately.
5. Reconcile actual charge and download the result promptly.
6. Never automatically repeat a paid POST after a lost response: the first job may already exist.

The script [check_asset_balances.py](../Tools/check_asset_balances.py) performs only three fixed official GET requests.
It reads provider-labelled lines from the local desktop All_api_keys.txt, refuses redirects and prints only sanitized
balance/status fields.

~~~powershell
python Tools/check_asset_balances.py --json
python Tools/check_asset_balances.py --providers Tripo Hyper3D --timeout 20
~~~

No script option accepts an alternate API base URL. In particular, the V2fun key is sent only to the user-specified
Chinese-region service; it is not automatically retried against the international domain.

## Tripo

### Animation capability update — 2026-10-31

Tripo Studio now documents **Text to Motion**, which generates a custom animation from a text description after rigging.
Its official game-character walkthrough documents clips up to 10 seconds and Multi-Stage Motion for combining actions.
This is distinct from the public API's preset-only `/animations/retarget` schema: no public text-to-motion API endpoint
was verified in this review. Studio entitlement, available balance and the displayed per-job price must be checked
separately before submitting a job.

Studio's Rig module includes Humanoid and Others; the latter covers quadrupeds, six/eight-legged creatures, birds,
snakes and fish. The API likewise documents these rig types plus `others`. However, its newer animation presets are much
narrower for creatures: quadruped/six/eight-legged walking and snake/fish marching are listed, while biped presets
include slash, shoot and hurt. Rig support alone does not establish an attack-animation library for every creature
shape. The help page for uploaded models is more restrictive than the general rig documentation, emphasizing
recognizable humanoids and standing quadrupeds. Validate an actual nonhuman model before depending on the broader
coverage.

For this project's new cartoon/nonhuman direction, retain compatible Titan skeletons and existing clips first. If the
hero needs a custom action, a single short Studio Text-to-Motion trial is a candidate after model selection; abstract
blobs and unusual creatures should keep a simple procedural attack until deformation is verified. This review submitted
no generation or rigging jobs.

Sources: [Studio rigging and animation](https://www.tripo3d.ai/product-doc/module-post-processing/rigging-animation), [Official character walkthrough](https://www.tripo3d.ai/blog/mesh-game-character-p-2), [Uploaded-model limitations](https://www.tripo3d.ai/help/features/is-it-possible-to-upload-my-existing-model-for-animation), [API rig types](https://developers.tripo3d.ai/zh/docs/animations-rig), [API animation presets](https://developers.tripo3d.ai/zh/docs/animations-retarget).

### Authentication, versions and lifecycle

Base: https://openapi.tripo3d.ai/v3

Header: Authorization: Bearer <key>

The current official documentation uses v3, not older /v2/openapi/task examples. Successful request bodies normally have
code=0 and data. Generation/processing requests return data.task_id.

Poll GET /tasks/{task_id}. The response data contains task_id, type, status, progress, output and sometimes
credits_consumed, error_code and error_message. Only success means a usable output; queued/running remain pending. Stop
on failed/cancelled, and on legacy banned/expired. Unknown statuses require bounded waiting, never an assumed success.
The output normally includes model_url and rendered_image_url. Imported-model processing may include normalize_scale and
normalize_translation; retain these when normalizing assets.

Official polling guidance is 1-2 seconds with a finite deadline. A local wait timeout does not mean the server cancelled
the job. Preserve task_id for a later status request.

Sources: [Introduction](https://developers.tripo3d.ai/zh/docs/introduction), [Lifecycle](https://developers.tripo3d.ai/zh/docs/task-lifecycle), [Query](https://developers.tripo3d.ai/zh/docs/task-query), [Migration](https://developers.tripo3d.ai/zh/docs/migration-v2-to-v3).

### Models and generation schema

Pin model explicitly:

- H3.1: v3.1-20260211.
- H3.0: v3.0-20250812.
- H2.5: v2.5-20250123.
- P1: P1-20260311, 50-20,000 faces.
- P2: P2-20260801, triangles 48-50,000 or quads 48-25,000.

P-series is optimized for controlled game-asset topology. H-series supports smart_low_poly; that option permits
500-20,000 triangles or 500-10,000 quads and has an added charge.

| Operation          | Method and path                     | Required/core input                                          |
|--------------------|-------------------------------------|--------------------------------------------------------------|
| Text to model      | POST /generation/text-to-model      | prompt (max 1,024 characters), model                         |
| Image to model     | POST /generation/image-to-model     | input (file token, public image URL or image-task ID), model |
| Multiview to model | POST /generation/multiview-to-model | inputs, model; front view required                           |
| Upload             | POST /files                         | multipart file; returns data.file_token                      |

Shared practical controls: face_limit, texture, pbr, texture_quality, model_seed, texture_seed, auto_size and export_uv.
Text generation additionally has image_seed and negative_prompt (max 255 characters). PBR=true forces texture=true. To
request bare geometry, explicitly set both false.

For image input, use clean PNG/JPEG reference images. The image-to-model reference also lists WebP, while the upload
reference lists JPEG/PNG; PNG is the conservative common format. Maximum image upload is 20 MB; maximum model upload is
150 MB.

P2 supports quad; P1 does not. H-only options should not be copied into P payloads. Generate-parts on H is incompatible
with textured output and is outside the MVP pipeline.

Leave export_orientation unset before rigging/animation. The official documentation warns that pre-rotated generation
can produce incorrect downstream orientation while still reporting success. Normalize orientation at final export or
locally.

Sources: [Versions](https://developers.tripo3d.ai/zh/docs/models-and-versions), [Text H](https://developers.tripo3d.ai/zh/docs/generation-text-to-model/standard), [Image H](https://developers.tripo3d.ai/zh/docs/generation-image-to-model/standard), [Text P](https://developers.tripo3d.ai/zh/docs/generation-text-to-model/p), [Image P](https://developers.tripo3d.ai/zh/docs/generation-image-to-model/p), [Multiview P](https://developers.tripo3d.ai/zh/docs/generation-multiview-to-model/p), [Upload](https://developers.tripo3d.ai/zh/docs/files).

Representative request bodies, not executed:

~~~json
{
  "model": "P1-20260311",
  "input": "<uploaded_reference_file_token>",
  "face_limit": 10000,
  "texture": true,
  "pbr": true,
  "texture_quality": "standard",
  "texture_alignment": "original_image",
  "model_seed": 10631
}
~~~

For text generation replace input/texture_alignment with prompt and, optionally, negative_prompt and image_seed. Keep
the silhouette, palette, pose and material description identical across the character family.

### Rigging, animation and processing

| Operation       | Method and path            | Schema and constraints                                         |
|-----------------|----------------------------|----------------------------------------------------------------|
| Rig eligibility | POST /animations/rig-check | input; inspect output.riggable and output.rig_type             |
| Rig             | POST /animations/rig       | input, model, rig_type, spec, out_format                       |
| Retarget        | POST /animations/retarget  | input=rig task ID; animation OR animations (max 5); out_format |
| Convert         | POST /models/convert       | input, format; optional with_animation, animate_in_place       |
| Remesh          | POST /mesh/decimate        | input, model, face_limit; optional quad, bake, smart_uv        |
| Retexture       | POST /models/texture       | input, model; optional texture_prompt, pbr, texture_quality    |

Recommended human rig model is v3.0-20260909 with rig_type=biped and spec=unreal; output can be fbx. Other documented
human specifications include tripo/mixamo/actorcore/unity/vrm. Bone-name support does not prove drop-in compatibility
with any particular UE animation skeleton.

Rig v3 uses presets including preset:idle, preset:walk, preset:run, preset:slash, preset:hurt, preset:jump and preset:
fall. Request at most five per batch. animation and animations are mutually exclusive. animate_in_place=true avoids
locomotion translation in clips. bake_animation is deprecated and ignored.

~~~json
{
  "input": "<model_task_id>",
  "model": "v3.0-20260909",
  "rig_type": "biped",
  "spec": "unreal",
  "out_format": "fbx"
}
~~~

~~~json
{
  "input": "<rig_task_id>",
  "animations": [
    "preset:idle",
    "preset:walk",
    "preset:run",
    "preset:slash",
    "preset:hurt"
  ],
  "out_format": "fbx",
  "animate_in_place": true,
  "export_with_geometry": true
}
~~~

Conversion supports GLTF, FBX, USDZ, OBJ, STL and 3MF; OBJ/STL are not animation delivery formats. Setting quad forces
FBX in the documented conversion flow. Mesh decimation v3.0 supports smart_uv; v1.0/v2.0 reject that option. Remesh
before skinning unless the resulting weights are explicitly revalidated.

Texture versions are independent of geometry versions. v3.5-20260815 enables fast texture mode and delight. Fast is not
a lower credit tier. Earlier versions do not provide the same delight behavior. Standard textures are sufficient for
this prototype; avoid 8K.

Sources: [Rig check](https://developers.tripo3d.ai/zh/docs/animations-rig-check), [Rig](https://developers.tripo3d.ai/zh/docs/animations-rig), [Retarget](https://developers.tripo3d.ai/zh/docs/animations-retarget), [Convert](https://developers.tripo3d.ai/zh/docs/models-convert), [Remesh](https://developers.tripo3d.ai/zh/docs/mesh-decimate), [Texture](https://developers.tripo3d.ai/zh/docs/models-texture).

### Prices, accounting and retries

| Generation                  | Bare geometry | Standard texture |
|-----------------------------|--------------:|-----------------:|
| H text                      |            10 |               20 |
| H image / multiview         |            20 |               30 |
| P1 text                     |            30 |               40 |
| P1 image / multiview        |            40 |               50 |
| P2 text / image / multiview |           100 |              110 |

| Operation                                                                 |               Credits |
|---------------------------------------------------------------------------|----------------------:|
| Rig check                                                                 |                     0 |
| Rig                                                                       |                    25 |
| Retarget                                                                  |      10 per animation |
| Convert, basic / advanced                                                 |                5 / 10 |
| Remesh v1 / v2 / v3 triangles / v3 quads                                  |     10 / 30 / 35 / 40 |
| Retexture standard / detailed / extreme                                   |          10 / 20 / 30 |
| H smart low poly / detailed texture / extreme texture / detailed geometry | +10 / +10 / +20 / +20 |
| H quad / generate parts                                                   |              +5 / +20 |

Advanced conversion is triggered by non-default quad, face_limit, flatten_bottom, flatten_bottom_threshold,
texture_size, texture_format, pivot_to_center_bottom or scale_factor. Do not blindly apply H add-ons to P-series
pricing.

P-series exact numbers were verified in the official pricing page's P-series table data, loaded by the public
PricingPage-WUV2iHaa.js asset on the review date. The initial text extraction exposed only the default H tab. The public
pricing page remains the stable citation.

GET /account/balance returns code=0, data.balance and data.frozen. GET /account/usage provides task_id, type,
credits_consumed and created_at. Creation freezes credits; success charges them; failed/cancelled tasks release them. A
technically successful but visually unsuitable result still consumes credits.

Example first-round character allowance: P1 image generation 50 + rig 25 + five clips 50 = 125 per complete character.
One base and at most one additional silhouette cost approximately 250 before revisions. Reserve up to 250 for two
complete reruns and 100 for necessary processing, within the 600 cap. Do not perform reruns unless the imported sample
actually fails acceptance.

Default concurrent pools documented: H generation 10, P generation 5, image generation 1, animation 10, processing 5.
For this small project, validate one sample first and use at most 2-3 concurrent production jobs. HTTP 429 may mean rate
(1007) or concurrency (2000); honor Retry-After. A lost create response must not cause a blind duplicate POST.

Sources: [Pricing](https://developers.tripo3d.ai/zh/pricing), [Billing](https://developers.tripo3d.ai/zh/docs/billing), [Account](https://developers.tripo3d.ai/zh/docs/account), [Errors](https://developers.tripo3d.ai/zh/docs/error-handling), [Limits](https://developers.tripo3d.ai/zh/docs/rate-limits).

### Rights and remaining checks

The terms distinguish Free Users and Paid Users; do not infer the account's category from promotional credits alone.
Record the account's applicable entitlement and generated-asset provenance. The terms allow lawful
commercial/non-commercial output use subject to their restrictions and third-party rights. This is a source summary, not
an assurance of exclusive rights.

Unverified until a sample is imported: skin quality, material reconstruction, exact FBX clip packaging, UE skeleton
compatibility, correct feet/pivot and animation after scale changes. Service status success is not asset acceptance.

Source: [Terms](https://developers.tripo3d.ai/zh/terms).

## V2fun

### Region, authentication and lifecycle

User-specified Chinese-region base: https://api.v2fun.art/api/v1

The official international mirror documents https://api.v2fun.ai/api/v1. The .art documentation was not directly
reachable during this review, but official indexed pages and the .ai documentation were readable. A key must not be
assumed valid across regions. The balance script sends the key only to api.v2fun.art.

Authorization: Bearer <key>. Documented scopes include model:mesh, model:texture, model:remesh, model:convert, motion:
animate, motion:query and asset:read. Missing scope can return 403.

POST is asynchronous by default; HTTP 201 means accepted. Save task_uuid and query GET on the same resource path
followed by /{task_uuid}. States: QUEUED, PROCESSING, COMPLETED, FAILED; poll around every 2-5 seconds.

For the next processing stage, use the returned asset path, not the task UUID. Downloads are exposed under
metadata/downloads with download_url and asset_path; links expire. Metadata appears as both an object and an array in
examples, and some result examples differ in string/array shape. Parse defensively and preserve non-secret task metadata
locally.

Sources: [Chinese generation](https://doc.v2fun.art/zh/api/core_functions/model-generation), [Authentication](https://doc.v2fun.ai/authentication), [Overview](https://doc.v2fun.ai/zh/api/overview), [Quickstart](https://doc.v2fun.ai/zh/quickstart).

### Endpoint schema

| Operation                  | POST path              | Core parameters                                                      |
|----------------------------|------------------------|----------------------------------------------------------------------|
| Text/image/multiview model | /3d_models/meshes      | prompt OR input_image OR input_images; model=pro/flash               |
| Texture                    | /3d_models/textures    | input_model; prompt/image guidance; keep_uv, pbr_texture, hd_texture |
| Remesh                     | /3d_models/remeshings  | input_model, mesh_count, mesh_topology=triangle/quad                 |
| Convert                    | /3d_models/conversions | input_model, output_format                                           |
| Motion search              | /motions/retrievals    | text_prompt                                                          |
| Motion retarget            | /motions/animations    | already-skinned input_model, input_motion, fps                       |

Generation defaults to with_texture=false. Multi-view input requires front, with optional left/right/back. Request
options.block=false for the explicit asynchronous workflow.

Conversion formats documented: obj, ply, stl, fbx, glb, usdz, 3mf. Animation expects a skinned character; motion data
can be the returned motion asset path or supported BVH input, and output is animated GLB.

~~~json
{
  "model": "pro",
  "prompt": "<one static Mother Tree, matching approved art direction>",
  "with_texture": true,
  "pbr_texture": true,
  "hd_texture": false,
  "options": {"block": false}
}
~~~

Submit that body to /3d_models/meshes and query /3d_models/meshes/{task_uuid}. If needed, then:

~~~json
{
  "input_model": "<returned_asset_path>",
  "mesh_count": 12000,
  "mesh_topology": "triangle",
  "options": {"block": false}
}
~~~

~~~json
{
  "input_model": "<returned_remeshed_asset_path>",
  "output_format": "fbx",
  "options": {"block": false}
}
~~~

12,000 faces is an example for a representative static landmark, not a requirement for every object.

Sources: [3D reference](https://doc.v2fun.ai/zh/api/reference/3d-models), [Remesh](https://doc.v2fun.ai/zh/api/core_functions/remesh), [Animation](https://doc.v2fun.ai/zh/api/core_functions/animation).

### Prices and balance

| Operation                      |                Credits |
|--------------------------------|-----------------------:|
| Base text/image/multiview mesh |                     20 |
| Add standard texture           |                    +20 |
| Add PBR                        |                     +2 |
| Add 8K texture                 |                     +5 |
| Separate texture job           | 20, enhancements extra |
| Remesh                         |                      5 |
| Convert                        |                      5 |
| Animation retarget             |                      5 |

A PBR textured static model plus remesh and FBX conversion is approximately 52 credits. At most two initial models plus
two full reworks total approximately 208, leaving 92 inside the 300 first-round cap. If local assets suffice, generation
cost is zero.

GET https://api.v2fun.art/api/v1/balance takes no body and returns a top-level numeric balance, for example {"balance":
0}. This is available balance after reservations for running tasks. It is not data.balance.

Sources: [Pricing](https://doc.v2fun.art/zh/pricing), [Balance](https://doc.v2fun.art/zh/api/reference/balance).

### Important gaps

- The animation capability is not limited to retargeting: V2fun also documents video motion capture at
  `POST /videos/motion_detections`, with optional preceding pose detection at `POST /videos/pose_detections` to identify
  the person. These are human-video workflows, not evidence of arbitrary creature motion generation. Text input to
  `/motions/retrievals` searches an existing motion library; it does not synthesize a new action.
  See [Motion retrieval](https://doc.v2fun.ai/zh/api/core_functions/motion-retrieval), [Video motion capture](https://doc.v2fun.ai/zh/api/core_functions/motion-detection),
  and [Pose detection](https://doc.v2fun.ai/zh/api/core_functions/pose-detection).

- A character tutorial mentions POST /motions/riggings and 14 normalized 3D joint points. The current reference does not
  supply a complete endpoint schema, joint ordering, permissions or price. Do not guess joint data or make this the
  character pipeline dependency.
- Failure-refund rules, partial refunds, cancellation endpoints and idempotency behavior were not clearly documented in
  the reviewed pages. Treat reserved costs as spent until an actual refund is observed.
- Fixed latency, concurrent-task caps and file-size limits were not established. Errors list 413 and 429 but do not
  provide those limits.
- General error documentation lists HTTP statuses, not a stable error-body schema. Never print raw error payloads in a
  credentials utility.
- Do not automatically retry an ambiguous POST timeout.
- Model deformation, remesh texture retention and UE animation compatibility require asset testing.
- Generated-asset commercial/repository redistribution rights require the account's current plan and input rights; the
  workflow article is not a blanket license.

Sources: [Character tutorial](https://doc.v2fun.ai/zh/examples/character-pipeline), [Errors](https://doc.v2fun.ai/zh/error-handling), [Commercial workflow](https://v2fun.art/en/blog/ai-3d-model-generator-commercial-project-workflow).

## Hyper3D / Rodin

### Authentication and workflow

Base: https://api.hyper3d.com/api/v2

Authorization: Bearer <key> on protected API requests.

| Operation  | Method/path        | Encoding and core parameters                         |
|------------|--------------------|------------------------------------------------------|
| Generation | POST /rodin        | multipart/form-data; explicit tier, prompt or images |
| Status     | POST /status       | JSON subscription_key from jobs.subscription_key     |
| Download   | POST /download     | JSON task_uuid from the top-level generation uuid    |
| Balance    | GET /check_balance | No body; optional usage_page for audit use           |
| Rig        | POST /rigging      | multipart/form-data; asset_id OR uploaded model      |

Persist top-level uuid, jobs.subscription_key and consumed. HTTP 201 can still contain an application error; verify
error is empty and uuid is present before polling. Do not expose subscription keys or signed download links in public
logs.

Wait at least 5 seconds before the first status request, then back off toward 30 seconds and honor Retry-After. Any
Failed job terminates the pipeline; download only when all jobs are Done. Download responses contain file names/URLs;
save files before links expire.

GET /check_balance normally returns HTTP 200 and {"balance": 10}. Adding usage_page=1 returns usage records that can
include account/key identifiers; the balance helper intentionally does not request or print these.

Sources: [Quickstart](https://docs.hyper3d.ai/en/get-started/quick-start), [Errors](https://docs.hyper3d.ai/en/api-specification/errors), [Status](https://docs.hyper3d.ai/en/api-specification/check-status), [Download](https://docs.hyper3d.ai/en/api-specification/download-results), [Balance](https://docs.hyper3d.ai/en/api-specification/check-balance).

### Generation and rig schema

Explicitly specify tier. Omitting tier can select older Regular behavior.

Recommended initial static/character sample form fields:

~~~python
data = {
    "tier": "Gen-2.5-Medium",
    "mesh_mode": "Raw",
    "quality_override": "12000",
    "geometry_file_format": "glb",
    "material": "PBR",
    "texture_mode": "medium",
    "TAPose": "true",
    "seed": "6031",
}
# Text: add data["prompt"], at most 1024 characters, and omit images.
# Image: multipart images (1-5); first image guides material.
# Prefer image-only geometry guidance when the approved reference already defines the asset.
~~~

Raw + <=20,000 faces + GLB meets the documented Fast-mode conditions. Explicit face count matters: default Raw/medium
can produce 500,000 faces. Supported formats include glb/fbx/obj/usdz/stl.

Rig from a completed generated asset:

~~~python
data = {
    "asset_id": "<generation_uuid>",
    "bone_template": "human_UE",
    "skinning_method": "core",
    "geometry_file_format": "fbx",
}
~~~

asset_id and uploaded model are mutually exclusive. Generated asset_id retains the confirmed texture. Uploaded model
does not preserve its embedded texture/material; non-None material requires an extra reference image. Other documented
templates include human, quadruped, bird, dragon and kaiju.

The rig interface permits up to 10 preset animations or matching FBX animation inputs, each FBX <=25 MiB and containing
one Take. A human/Walking example is documented, but compatibility of that specific preset with human_UE and
compatibility with UE Manny remain unproven. Verify these before committing to the pipeline.

The current rigging schema also supports custom skeleton hierarchy or a natural-language `skeleton_description`, and
`rigging_mode=modify` for skeleton changes. That description concerns body structure and limbs; it is not a documented
text-to-motion prompt. The verified animation inputs remain stock presets or uploaded clips. Do not infer a publicly
callable motion-generation API from ACT research or product previews.

Sources: [Gen-2.5](https://docs.hyper3d.ai/en/api-specification/rodin-gen2-5), [Rigging](https://docs.hyper3d.ai/en/api-specification/rigging).

### Prices, permissions and refunds

| Operation                   |               Credits |
|-----------------------------|----------------------:|
| Base Gen-2.5                |                   0.5 |
| Gen-2.5-Extreme-High        |                  +0.5 |
| Extreme-high texture        |                    +2 |
| Base rigging                |                   0.5 |
| Base texture-only           |                   0.5 |
| Image generation            |                   0.5 |
| Bang model splitting        |                   0.5 |
| Status / download / balance | No additional credits |

A basic generation plus basic rig is estimated at 1 credit before any separately charged options. Use only the
documented base settings for the first sample. The first-round cap is 10; do not treat the user's 223 as an instruction
to spend it.

Official pricing descriptions conflict on HighPack: the Gen-2.5 endpoint says unlisted parameters add no charge, while
the API product page lists HighPack +1. Do not enable HighPack, Extreme-High or extreme-high texture in this prototype.

Official help states rejected requests do not charge and failed submitted tasks refund credits. Rigging reserves credits
and charges after successful packaging; failed/cancelled tasks release reservations. Verify the balance/usage before
recording a refund as received.

The documented API plan entitlement is Business on the public subscription page; a key can return
API_SUBSCRIPTION_PLAN_TOO_LOW even if a web wallet has credits. The user's web balance is not proof of usable API
balance or entitlement.

Sources: [Gen-2.5 pricing](https://docs.hyper3d.ai/en/api-specification/rodin-gen2-5#pricing), [API product](https://hyper3d.ai/features/api), [Failed-job accounting](https://hyper3d.ai/help/articles/are-credits-deducted-when-a-generation-fails), [Plans](https://hyper3d.ai/pricing?lang=en).

### Other documented endpoints and excluded dependencies

| Capability   | Endpoint / version                        | Notes                                                   |
|--------------|-------------------------------------------|---------------------------------------------------------|
| Gen-2        | POST /rodin, tier=Gen-2                   | Explicit legacy fallback; face-count presets differ     |
| Gen-1/1.5    | POST /rodin, Sketch/Regular/Detail/Smooth | Do not reach it accidentally through missing tier       |
| Agentic      | POST /rodin_agentic                       | Beta; basic/standard/pro 0.5/1/2; singular job response |
| Texture only | POST /rodin_texture_only                  | model + image; base 0.5                                 |
| Bang         | POST /bang                                | Splitting; unnecessary for current required assets      |
| Images       | POST /images                              | Synchronous data[0].b64_json; not the job polling flow  |

Sources: [Gen-2](https://docs.hyper3d.ai/zh/api-specification/rodin-gen2), [Gen-1/1.5](https://docs.hyper3d.ai/en/api-specification/rodin-gen1-1-5), [Agentic](https://docs.hyper3d.ai/en/api-specification/rodin-agentic), [Texture](https://docs.hyper3d.ai/en/api-specification/generate-texture), [Bang](https://docs.hyper3d.ai/en/api-specification/bang), [Images](https://docs.hyper3d.ai/en/api-specification/images).

### Retention, rights and remaining checks

API inputs/outputs are documented as retained in the active system for seven days, not added to the public gallery and
not used for training. Archive paid outputs promptly. Rodin output terms do not add a separate output-use restriction
beyond the applicable agreement, law and third-party rights; do not substitute ChatAvatar terms.

Unverified: actual API entitlement for this key, which wallet contains the 223 credits, human_UE clip compatibility,
deformation quality, material import and whether any selected animation option adds a charge. Inspect one complete
sample and returned consumed values first.

Sources: [Data retention](https://docs.hyper3d.ai/en/legal/data-retention-policy), [Rodin terms](https://hyper3d.ai/legal/terms).

## Common asset acceptance gate

A generated preview image is not sufficient. Before repeating a paid workflow:

- Confirm the file opens and has the requested mesh, textures, scale, forward axis and pivot.
- Inspect the actual triangle count, material count and texture sizes.
- Check detached/fused limbs, underside holes and visibly broken silhouettes.
- Import the representative model into the new UE project and play idle, locomotion and attack/hit clips.
- Check feet and deformation at the prototype's minimum and maximum body size.
- Confirm collision proxies and animation-driven visuals do not alter authoritative combat rules.
- Preserve source references, task/model versions, sanitized generation settings and a local file hash.
- Record license/provenance independently of appearance approval.

No supplier is assumed stylistically identical to another. Use shared concept references and normalize palette/material
roughness, but keep the full character family on one verified pipeline. Cross-provider work should be limited to
different asset categories such as a single static Mother Tree.
