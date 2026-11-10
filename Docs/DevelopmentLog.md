# Development log

## 2026-10-31 — Ember speech bubbles and a single 5-second reply

- Ported the existing `SEmberConversation` Slate `SCanvas/SBorder/STextBlock` bubble from
  `E:/UE_projects/EmberZeldaStandalone/Plugins/EmberDecision/Source/EmberDecision/Private/EmberConversation.cpp` into
  `VerdantEmberSpeechWidget.h/.cpp`, attached at HUD Z30 to DeepSeek-backed `LastSpeech/SpeechUntil`. No Ember local
  inference service was imported. Retired and deleted the old Canvas `VerdantSpeechBubbles.h/.cpp` implementation.
- `Character.BeginDialogueReply` uses one 5-second deadline for the bubble and stopped movement. Each frame, the NPC
  faces the player when within hearing range, otherwise the actual recipient; its existing plan resumes afterward.
  Other speech or API results cannot overwrite or extend the reply. Death clears it and real damage still applies.
  Warned attack/steal actions wait until the reply expires. Failures now appear as scene notifications after composing.
- A narrow reproduction did start PIE and attach the native debugger before this port. The Publish breakpoint could
  not bind (“no executable code”); only an engine UpdateTime stack and ordinary values were captured, so the old bubble
  cause was not established at runtime. That older PIE could automatically issue NPC API requests.
- The editor exited normally while waiting for a second bubble breakpoint; this was not the screenshot crash. Both
  agent breakpoints were removed, the user's 9 exception breakpoints were preserved, and the debug session ended.
  The narrow-debug record is `Saved/Validation/npc-reply-debug-triage.json`.
- The final build check succeeded with exit code 0 (`Saved/ember-reply-five-seconds-build.log`). UBT reported “Target is
  up to date”, 0 actions, in 1.43 seconds; this command did not compile files again. The module DLL is newer than the
  relevant source and `VerdantEmberSpeechWidget.cpp.obj` exists, confirming the module contains the new implementation.
  The new version has not undergone visual or gameplay acceptance. Earlier test records below do not validate it.

## 2026-10-31 — Inclusive 5x conversation height limit

- Increased conversation eligibility to a taller/shorter height ratio of at most 5, including exactly 5x. The policy
  default, `MaxHeightRatio=5` configuration, dialogue initialization fallback and constructor clamp ceiling now agree.
  The true 3D 40 m hearing radius is unchanged.
- All 38 pure-policy checks passed (`Saved/Validation/dialogue-height-ratio-5-policy.json`), covering the inclusive 5x
  boundary, actor-order symmetry, just-over-limit rejection both ways, combined 40 m/5x limits and cases now allowed
  beyond the former 2x restriction. No API requests or PIE were run. Native compilation passed with exit code 0 in
  21.55 seconds (`Saved/dialogue-height-ratio-5-build.log`), and the editor reopened with the new build loaded. Earlier
  2x and 31-case results below remain historical records. No visual or broader gameplay acceptance is claimed.

## 2026-10-31 — Consistent conversation availability and persistent audience selection

- Added one `GetPlayerConversationAvailability` check for overhead status, audience choices, default shortcuts and send.
  It reports TooFar, SizeMismatch, Cooldown, Busy, Unavailable or Ready using the true 3D 40 m hearing radius. Overhead
  labels show names and state without metre values. Names remain visible to at least 60 m, using 1.5 times the hearing
  radius, so out-of-range status is visible; hearing itself remains 40 m.
- The audience dropdown refreshes through Slate every 0.1 seconds, including while the world is paused. It reuses
  entries and refreshes list membership only when that membership changes, rather than rebuilding or sorting every
  frame.
  Busy NPCs remain listed with their state instead of being filtered through `CanAcceptPlayerMessage`.
- Selecting a busy NPC shows a brief instruction to return to the scene and wait for the current response. Availability
  changes restore the relevant controls and clear stale errors, avoiding a wait inside the paused composer.
- A selected NPC leaving range remains selected as an unavailable entry. The draft is preserved and the audience does
  not switch to broadcast. Sending becomes available again after the NPC returns and the other conditions are met.
- Native compilation passed with exit code 0; the final incremental build took 8.71 seconds
  (`Saved/dialogue-live-status-build-final.log`). No API requests, PIE, screenshots or visual tests were run; gameplay
  acceptance remains with the user. Earlier rule-check results below do not validate this interaction change.

## 2026-10-31 — Four natural prompts and one 40-metre hearing radius

- Default shortcuts now offer request food, invite company, negotiate gathering and offer food to the mother tree.
  Each inserts a natural proposal into the shared draft; none binds a model decision or guarantees a result.
- Increased the default hearing radius from 20 m to 40 m (`[Verdant.Dialogue] RadiusCm=4000`). HUD target selection,
  actual listeners, Options distance candidates, Population relationship/shared-event context and speech-bubble distance
  all read `Dialogue.GetHearingRadiusCm`. The 2x height limit, one-request direct speech and 3-listener broadcast cap
  remain.
- Execution distances are independent: giving still requires 11 m, stealing 3 m, and land transfers retain their own
  nearby-target requirements. A larger hearing radius does not permit remote food or ownership transfers.
- All 31 pure-policy boundary checks passed (`Saved/Validation/dialogue-radius-40m-policy.json`). After ending the
  existing Play session as requested, the normal native build succeeded with exit code 0 in 21.59 seconds
  (`Saved/dialogue-radius-40m-build.log`). No PIE, gameplay tests or API calls were initiated for this revision; visual
  and gameplay acceptance remain with the user. Previous build and API results below are historical checkpoints.

## 2026-10-31 — Shared composer, explicit audiences and autonomous negotiation

- Replaced per-NPC composers with one shared draft. The interface contains 3 default topic starters, a broadcast/NPC
  audience selector, free text, send and cancel. T opens with broadcast selected; topic starters only fill the draft.
- Direct speech creates one request for the selected eligible NPC, and that answer does not propagate to bystanders.
  Broadcast selects at most 3 initial eligible listeners. With no listeners it still shows the player's bubble, uses
  no API call and counts as a successful send. Player threads allow at most 3 answers; proactive NPC chains retain 2.
- Retained the 20 m and 2x height eligibility rules, 5-second player cooldown, shared 20-second NPC initiative interval,
  real pause while composing, and deferred HTTP completion with fresh validation after resuming.
- DeepSeek now decides acceptance, refusal, counteroffers and actions. Removed hardcoded personality, trust, gift and
  fixed-deal social gates while retaining life, inventory, capacity, food-tier, ownership, distance and cycle
  constraints.
  The 14 existing action names remain execution interfaces, not dialogue-menu choices or newly invented abilities.
- `share` executes the requested ratio from 0 through 0.9; `cede` can transfer management to any valid nearby actor.
  New follow, donation or attack plans clear conflicting plans. Wild NPCs can pledge donations, and the rule brain no
  longer immediately overturns accepted promises using old preferences.
- Two real API examples passed structure/action checks with 1,453 total tokens, but the share dialogue incorrectly
  described 15% as “十五成”; `Saved/Validation/free-dialogue-live.json` retains that failed wording. The prompt now
  requires percentages throughout. The focused repair check passed with one request, 749 tokens and 1.656 seconds
  (`Saved/Validation/free-dialogue-share15-fix-live.json`): `share/target=2/amount=0.15`, both mentions correctly said
  15%, and no claim that the player had already fulfilled the deal.
- Native build passed with exit code 0 (`Saved/dialogue-shared-free-build.log`). SocialProbe social assertions were
  updated but not run; other old in-memory baselines are obsolete. These synthetic API fixtures and compilation do not
  validate world actions. No full-suite, PIE or visual acceptance is claimed; gameplay review remains with the user.

## 2026-10-31 — Paused speech input and shared dialogue pacing

- Opening the composer with T now pauses the game world. Enter closes it and resumes only after a non-empty message
  enters the queue successfully; Esc cancels and resumes without consuming the speech cooldown.
- A successful player send starts a 5-second game-time cooldown. Pressing T during that cooldown shows the remaining
  time without opening the composer or pausing the scene. Empty or rejected sends do not consume it.
- Player and NPC speech now appears above each speaker. Chinese text wraps by measured width and long text paginates
  in groups of 5 lines, supporting up to 500 characters instead of truncating the display at 42 characters.
- HTTP completions arriving while paused are retained until the resumed dialogue Tick, which rechecks current actor
  life, plans, distance, height and legal actions before publishing dialogue or applying changes.
- Removed individual NPC response timers and NPC-pair dialogue cooldowns. Proactive speech uses one shared 20-second
  global interval, first checked at game time 10 seconds and checked every 3 seconds while idle. Replies to the player
  do not wait for that proactive interval. New-context deduplication and the existing candidate/queue gates remain.
- The one-call DeepSeek protocol is unchanged: relevant replies use the four-field JSON; irrelevant input returns the
  short `无关` marker and ends silently. Native compilation passed with exit code 0
  (`Saved/dialogue-compose-bubbles-build-final.log`). No PIE, API requests or screenshots were run for this revision;
  visual and gameplay acceptance remain with the user.

## 2026-10-31 — Single-request cloud dialogue and GPU release

- Replaced local relevance screening with one DeepSeek request per eligible NPC. The same response either returns
  literal `无关`, ending silently, or the `say/action/target/amount` JSON. No separate cloud gate or `stop` parameter.
- Eligibility uses actual 3D distance of at most 20 m and a taller/shorter height ratio of at most 2. Each speech event
  selects at most 3 candidates; each dialogue chain permits at most 2 actual NPC replies.
- Requests disable thinking, use temperature 0.3 and cap output at 256 tokens. The provider rejected
  `response_format=json_object`, so the prompt constrains JSON and the game validates it before applying actions.
- Normal editor/game launchers no longer bootstrap local inference. Portable staging no longer copies the local
  inference runtime or weights; existing models and old packaged builds remain unchanged.
- After exact process identity checks, stopped this copy's StartLux service and the stale Qwen process from the earlier
  build of the same game. Ports 8066, 8081 and 8090 had no listeners afterward. Total GPU usage was 888 MiB at that
  observation, not an isolated performance benchmark or a promised steady-state budget.
- Distance/height boundary checks passed 25/25 (`Saved/Validation/cloud-dialogue-switch-policy.json`). Six API cases
  passed for the preceding `不相关` protocol with 3,620 total tokens (`cloud-dialogue-switch-live.json`); those results
  are historical. The final `无关` protocol compiled successfully (`Saved/cloud-dialogue-final-build.log`) and passed
  3/3 targeted API fixtures (`Saved/Validation/cloud-dialogue-switch-wuguan-live.json`), with one request per case and
  no retry. The two irrelevant cases used one output token each; the valid handoff returned dialogue and `give` together
  in 35 output tokens. Total usage was 1,584 tokens; observed latency was 1.03–1.83 seconds. These synthetic fixtures
  did
  not exercise the game scheduler or world actions. No claim of full 20-NPC gameplay acceptance.

## 2026-10-31 — Historical source integration checkpoint before cloud-only switch

- Saved the five-scale terrain, ecology and player-scale changes together with the food-tier, growth and respawn rules.
- The current source adds 20 resident types from the character catalog, instance relations, nearby speech events,
  StartLux relevance screening and queued DeepSeek responses. Configured launchers can bootstrap the secondary service;
  direct editor/validation use requires starting it explicitly. See `LocalAI.md`. The full character catalog remains a
  design library, not a claim that every type is integrated.
- Included runtime audio, the saved sunrise map/materials, their source records and preparation tools. Licensed local
  audio payloads remain excluded from Git and must be rebuilt from the user's vendor library, as described in
  `../Assets/Audio/README.md`.
- `Tools/stage_population_assets.py` is a local authoring helper: it requires the original Titan project and the local
  `Saved/CharacterCatalog/all_asset_classes.json` asset-registry export. That cache is not included in Git. The selected
  runtime packages and generated population tables are included; a fresh checkout cannot rerun the helper without its
  authoring inputs. Ecology preparation likewise retains the original local library/Blender path assumptions.
- The catalog PDF/XLSX retain their original design snapshot; Markdown and runtime-policy notes describe the current
  implementation status. The duplicate catalog delivery ZIP and preparation logs are kept outside version control.
- Commit preparation used static source/dependency, metadata and LFS checks only. It did not compile, start Play, make
  model requests or capture the editor. Earlier build/import observations below apply to their recorded checkpoints;
  they are not an end-to-end gameplay acceptance of this combined source snapshot.

## 2026-10-31 — Visible sunrise and direct light

- Added the editor-only, explicitly invoked `Verdant.InspectSunrise` authoring command. It samples existing terrain
  heights and writes a numeric horizon report, without spawning a game world or using screenshot capture.
- Identified terrain blocking the old sun: original 8-degree sun versus about 36-degree ridge at azimuth48. Selected
  azimuth96/elevation25 to clear the roughly22.6-degree arrival ridge.
- Saved a distinct solar disk, localized warm dawn band, aligned directional sunlight/dynamic shadows and subtle
  screen-space Light Shaft Bloom. No global volumetric fog or shaft occlusion was introduced.
- Native Editor build passed; map/material save, sun vector alignment, shadow flags and shaft flags verified. No shader
  compile errors found. User visual review remains pending; horizon estimate excludes props/trees/clouds and actual
  spring-arm collision.

## 2026-10-31 — Quiet morning sky for Mosslit Paths

- User judged the concept-art sky too sunny relative to the music and requested an early-morning mood. Viewed the
  supplied music artwork and read the existing Mosslit Paths production prompt.
- Saved a restrained slate/pearl sky with a smaller-brightness, diffuse solar disk and weaker golden halo; direct sun
  intensity reduced from 21 to 13. Preserved sun direction, cloud shape, sky-fill intensity, exposure and fog; no new
  fog or scene-content/audio changes.
- Saved shader/material/map edits and the reusable setup values. `Assets/TitanSky/dawn-report.json` contains the backup
  and unchanged-setting checks. No automated Play or screenshot capture; user visual review pending.

## 2026-10-31 — Project palette on the accepted sky

- User accepted the sky structure and asked for the original concept-art palette. Viewed both
  `Assets/Concepts/valley-art-target-v01.png` and `fractal-world-v01.png`.
- Replaced pink/purple sky with pale blue/blue-grey and cream/honey sunlight; cloud albedo is ivory instead of pink.
  Updated the reproducible sky script and saved material/map assets.
- Verified unchanged sun rotation/intensity and cloud-layer dimensions. No geometry, layout, exposure or gameplay
  changes. Backup and verification record: `Assets/TitanSky/palette-report.json`. Visual colour acceptance pending user
  review.

## 2026-10-31 — Reference-matched sky scope

- User clarified that only the B-612 reference sky is the target. Added a project-native Unlit/IsSky dome material with
  golden horizon, peach/pink transition, blue-violet zenith and one low solar disk; existing soft volumetric clouds
  remain above it.
- Matched the actual sun direction to the disk, numerically checked their vectors, and corrected Python Rotator calls to
  explicit named axes. The background dome is outside cloud trace distances and the ground-fog cutoff excludes it.
- Saved the map and materials, verified shader statistics and no shader compile errors. Updated launch-map regeneration
  to include the sunset. No terrain/foliage/gameplay/global-post-process changes, no automated Play or screenshot
  capture. Visual acceptance remains pending.

## 2026-10-31 — Haze correction and B-612 reference review

- User reported the first sky pass was gray and lacked lighting. Confirmed Mie coefficient 0.55/km against UE default
  0.003996/km, corrected to 0.0025/km, reduced height fog and disabled extra fog/cloud occlusion. Applied and saved to
  Valley without starting PIE; sun intensity and exposure unchanged. Visual acceptance remains pending.
- Retrieved the user-linked author's article through the public FxTwitter mirror because direct X access returned 403.
  The exact attached B-612 board appears in its AI-generated scene-reference section. Author describes concept images ->
  selected Tripo props -> Blender cleanup/assembly -> Unity integration and multi-view iteration. Specific
  GI/shadow/HDRI techniques are not established by that article.
- Future quality target: a detailed storybook diorama with dense clustered foliage, blended ground/rock transitions and
  clear warm-key/cool-fill lighting. Start with one compact playable area rather than uniformly scattering a few
  low-detail assets across the full valley. Reference evidence is retained under `Saved/ReferenceResearch`.

## 2026-10-31 — Clear-after-rain environment first pass

- Applied and saved six coordinated environment actors in Valley: atmosphere, sun, sky fill, height/volumetric fog, soft
  clouds and an unbound fixed-exposure colour grade.
- Migrated six Titan soft-cloud packages (13.97 MiB). Project-local cloud material bypasses the original volcanic plume
  in both colour and density; original source packages remain unchanged.
- Saved scripts reproduce the setup without changing terrain or gameplay code. The previous map is backed up under
  `Saved/EnvironmentBackup`; the exact path and settings are in `Assets/TitanSky/environment-report.json`.
- Verified one actor per environment class, saved cloud material linkage and manual-exposure settings, zero dirty
  packages and no material-compilation error in the editor log. No automated Play, screenshot capture, paid generation,
  renderer migration or C++ rebuild was needed. Visual quality and performance remain pending user review.

## 2026-10-31 — NPC inspection at arrival and overhead health

- Injured, living, visible NPCs now show a compact health bar below their name; full-health and dead actors hide it. The
  bar uses actual Health/MaxHealth and the existing visibility gate. NPC name height and metre labels use the species'
  actual capsule height.
- `Config/DefaultGame.ini`, `[Verdant.Preview] bStartAreaCreaturePreview=True`, temporarily places the existing penguin
  (Miya/1), Jellybean (Taon/2), and desert ferret (wild resident/4) 4.5 m ahead of the player, spaced 3 m apart. No
  duplicate actor IDs or additional resource holders are created.
- These three wait in place until attacked or assigned follow/donation/migration movement, then resume their normal AI.
  This temporarily pauses their usual autonomous routines; set the option to False and restart Play to restore original
  placement and behaviour.
- Native Development Editor compilation succeeded. Editor reopened; gameplay and visual review remain with the user. No
  PIE or screenshots were triggered.

## 2026-10-31 ? independent project bootstrap

- Established a fresh repository over SSH; no earlier project history imported.
- Recorded the user-approved continuous-growth / NPC-world concept and first-playable boundaries.
- Created the independent Verdant UE 5.8 module and build targets.
- Started parallel official API documentation research for Tripo3D, V2fun and Hyper3D.
- Defined an initial coherent art direction and conservative spending caps; no paid generation submitted.
- Existing Ember working files are left untouched.

Validation: repository clone succeeded. Engine compilation and gameplay validation pending.

## Toolchain checkpoint

- First build found the installed UE 5.8.3 requires BuildSettings V7 for shared editor build products. Updated both
  targets to the installed engine setting.
- Bootstrap commit pushed successfully: 2d8856d.

## API and source-art checkpoint

- Read official Tripo v3, V2fun v1 and Hyper3D API documentation; endpoint, pricing and uncertainty notes are in
  GenerationAPIs.md.
- Authenticated read-only balance checks succeeded: Tripo 3000, V2fun 5100, Hyper3D 223. Zero paid generation tasks.
- Found CC0 original Quaternius character/animation/nature archives. Preparing a fresh import and animation validation;
  no derived Ember asset copied.
- Independent VerdantEditor build passed on UE 5.8.3 (BuildSettings V7).
- Corrected README UTF-8 title after the initial PowerShell pipe encoding issue.

## First integrated runtime findings

- Core gameplay module compiles. Fresh map and palette materials generated successfully.
- First real launch exposed an accidental APawn::Restart override in the R-key handler, causing repeated map travel.
  Renamed the handler to RestartJourney. Rider currently indexes only the reference project, so the attempted semantic
  rename could not operate on the new project; three direct references were corrected locally.
- Local Qwen service loaded successfully and returned a real Chinese JSON negotiation response in about 0.25 seconds in
  a simple probe.
- Runtime validation remains in progress; no playable-complete claim yet.

## Original gameplay milestone

- Added an original procedural valley, continuous size/health/reach growth, timed melee, dodge, loot, harvesting and
  sharing.
- Added three territorial food sources, NPC harvesting/flight/migration, true delivery pledges and Mother Tree
  restoration.
- Added Chinese HUD, dialogue input, real local model HTTP requests and validated action execution.
- Full editor compilation passes after refreshing the UBT source manifest with -gather.
- Added runtime smoke and world-behaviour probes; gameplay/visual pass is still pending final character import.
- Fixed start-up map reload caused by the APawn lifecycle collision and corrected invalid/contradictory dialogue actions
  during review.

## Art and live-world verification milestone

- Rebuilt 3 CC0 characters on a shared rig, imported 9 clips and 6 nature meshes from original vendor files. No
  generated-asset credits spent.
- Fixed Blender hidden bone-display geometry, material slots and source rest-pose differences before final import.
- Rendered game smoke passes 16/16, world delivery/territory tests pass 13/13, actual in-game local AI integration
  passes 3/3.
- Improved negotiated action schema after retaining and inspecting failed small-model cases; 3 selected protocol cases
  now pass.
- Captured and inspected stable gameplay/dialogue images. Original teleport-frame image was rejected and replaced.
- Independent Windows packaging underway. Explicitly included runtime-loaded engine basic shapes after inspecting cook
  output.

## First playable / v0.1

- Packaged Windows executable passed 16 smoke + 13 world-behaviour + 3 real-AI integration checks.
- Verified the staged model hash, local AI runtime, Chinese UI and packaged runtime-generated geometry.
- Added explicit engine basic-shape cook dependencies and disabled unused Android file serving.
- Portable entry point: Builds/Windows/Play.cmd. This folder includes the local model and licenses.
- User subsequently authorized continued autonomous playtesting and fun/quality iterations after this prototype
  milestone. Preserve this first playable while making the next iteration.

## v0.2 playability checkpoint

- Preserved the v0.1 Windows package and tagged source; developed on iteration/playability-v0.2.
- Fixed strafing attack direction, interrupted ghost attacks, animation/hit-clock disagreement, roll recovery,
  conversation interruption and growth-camera framing. All 40 explicit combat/input checks passed at 1280x720, including
  the 4.5m body above the expanded HUD.
- Added contextual legal gathering/tax information, actual delivery progress and carried-food visuals. Held E gathers
  repeatedly; Shift+F makes a bounded bulk offering.
- Kept real food reserves on wilderness opponents so combat loot remains useful; no synthetic kill reward.
- A normal-resource engine-input journey passed 9 checks in 40.78 simulated seconds: first gathering at 14.61s, height
  0.81m to 1.59m, 19 real food looted, player22 + Miya5 actually delivered. No teleports, grants or frozen NPCs were
  used in this journey. It is not yet a full100-food completion.
- Deterministic 90s policy comparison exposed a genuine tree-canopy navigation blockage. Rider/LLDB captured the actual
  collider and stopped branch; replaced foliage collision with trunk capsules. After the fix, opening highland changed a
  resident route and caused30 actual non-owner food harvests versus0 in the closed control, followed by resource-driven
  migration. Failed baseline retained; see NavigationDebug.md.
- Narrowed paths, added nearby natural scale references and reduced haze. Added six reproducible original mono48kHz SFX,
  imported and ran the journey with the audio device enabled. Waveform checks pass; physical listening has not been
  verified. Paid generation spend remains0.
- Native desktop-control helper was unavailable after retry/reinitialization. Validation uses engine-input replay, not
  physical desktop automation or a human fun rating.

Next: verify normal-resource completion, make the resulting world state easier to compare, then package separately from
v0.1.

## v0.2 world-outcome and full-route checkpoint

- Added an immutable restoration-season record with the opening/restoration population, per-grove control/access/tax and
  actual per-person donation ledger. A later death or policy change affects the live world without rewriting the earlier
  record. Fully free/open groves are now distinct from the initial divided/taxed state.
- Reviewed and fixed three concrete boundaries: a legal0.01-food pledge no longer stalls; gifts earn trust proportional
  to the actual transfer and cap gift-only goodwill at12; dialogue requests, execution and asynchronous replies
  revalidate physical conversation distance. Leaving range also releases the dialogue UI/input.
- All25 world-rule checks passed, including the new fractional-transfer, distance, accounting and season-snapshot cases.
- A complete normal-resource engine-input route restored100 food at about101s and exited at104s. Contributions were
  player55, Miya5, Lanshi20 and Taoen20. Four additional real-model requests received replies; two actual policies
  became free access. The resulting state was3 living main residents,1 living wilderness NPC and3 free groves. Logs show
  Lanshi independently defeated the red-backed raider during the route. This is an observed run, not a claim that every
  model response or route has the same result.
- The same route travelled396m, without teleporting, granting resources/size or freezing residents. The photographed
  end-state UI was readable at1280x720. The next capture route walks back physically for a wider view and reports
  restoration time separately from capture time.
- A brief early-scene Unreal CSV profile at1600x900 on RTX5060 Laptop measured mean frame5.15ms / GPU5.14ms, game2.01ms,
  draw3.44ms. The initial apparent low-FPS concern was a log-counter wrap, not a measured rendering bottleneck. No
  speculative graphics downgrade was made. A native-package sample follows.
- Build tooling now archives each version separately and stages model/visual licenses. It does not switch the launcher
  until validation succeeds. Source provenance refresh preserves audio and nonzero spend fields from other asset
  producers.

- Fixed the2.49 growth-stop boundary: the player can reach2.5, digest remaining nutrition, and spend food on healing at
  maximum size. Full-size/full-health eating preserves inventory. All45 combat/input checks and16 core smoke checks pass
  after this change.

## v0.2 native-package verification

- Versioned Windows BuildCookRun succeeded.100 native checks passed, including a full-resource route restoring at99.75s;
  all five ordinary validation processes exited0.
- Retained a native boot-CSV shutdown failure (777003), attempted native debugging, documented the unbound engine-source
  breakpoint, and performed matched option comparisons. CSV-only and CSV+GPU-stat smoke failed on exit; GPU-stats-only
  and ordinary full journeys exited0. No engine/crash handler was patched. Formal gameplay tests are independent of
  optional instrumentation.
- Re-tested portable AI from a cold start using its copied runtime, model and DLLs; real native dialogue passed.
- Fixed the PowerShell5 report-merging array wrapper and verified five latest actual case records survive JSON
  round-trip, with earlier failed cases retained separately.
- Verified the original v0.1 executable hash is unchanged; prepared v0.2 launcher promotion and packaged
  controls/licenses/demo instructions.

## v0.3 first integration

- Started the user-authorized two-hour hidden iteration at2026-10-31 05:47:49 Asia/Shanghai; no game/editor window was
  shown.
- Added distinct food reserves, matching donations from real public help, net-gift/spent-credit accounting, follow
  willingness and peaceful transfer of a single controlled grove. Requests and execution share authoritative limits.
- Added paid B-key grove tending and player policy cycling (free/taxed/closed), preserving actual food and state
  changes.
- Companions now intervene against threats, avoid their party, preserve agreements when attacked by outsiders,
  retreat/heal from real food and retry blocked travel without remote fulfilment.
- Added one-shot late-click attack buffering, cancelled by stagger/dialogue/dodge, and targeted input checks.
- First editor integration compiles. Social/world/NPC/combat/real-AI probes all exited0. These are fixtures/integration
  cases, not yet the v0.3 normal-resource full route.
- Render review found Canvas text missing despite valid Slate text: a file-backed Slate font has no UFont object
  required by this engine's Canvas path. Also found over-wrapped footer context. Both are explicitly pending the next
  visual fix; not claimed visually complete.

## v0.3 second integration / feature freeze

- Fixed Canvas labels with a GC-held runtime UFont, widened footer text slots, and verified actual Chinese labels/height
  ruler/damage arcs in hidden720p captures. Offset overlapping damage numbers and omit place labels over the player or
  corner HUD.
- Separated per-NPC transcripts/status from the in-flight RequestNPC; real-model callback-switch tests pass. Explicitly
  provided production-validated legal actions to the model after retaining a case that confused blocked cede with close;
  the tightened real-model refusal check now passes.
- Added complete-exchange bounded memory and rejected cyclic follow chains. NPCs now use their leader's area when
  escorted away from their birthplace, avoid endless attacks through walls and can perform bounded, paid soil care.
- Improved the validator to require a fresh report with zero reported failures as well as a zero process exit. A first
  steward route reported a failed ownership assertion despite editor exit0; it was retained, then the route earned more
  real help instead of weakening the game rule.
- Complete public and peaceful-steward routes now both pass, preserving all3 main residents and creating distinct
  ownership/access outcomes.
- Closed/open600-second natural simulations each checked36007 frames, with no invariant failures or reported
  stationary-travel suspects. Both eventually settle with sufficient stock; this does not demonstrate endlessly
  escalating autonomous stories.
- No generated models or paid API tasks were added. Preparing a separate native v0.3 package; v0.2 remains intact.

## v0.3 native validation and editor handoff

All eight hidden native cases completed successfully at 2026-10-31 07:14 Shanghai. Final handoff resumed after an
interrupted session; no claim of continuous work during the interruption. Gameplay source: 2d7ba41. Stable v0.2
preserved. Added reproducible independent editor-project export, including source, uncooked content, configuration,
editor module and local AI; excluded old build output, git history and caches.

Independent editor export verified at E:/UE_projects/VerdantAccord_v03: clean editor-module compilation succeeded;
Source/Content/Config files hash-match the repository; hidden Smoke and real-model AI checks both returned 0 with fresh
zero-failure reports. Git delivery remains on iteration/living-world-v0.3; automatic approval declined the optional
main/tag publication, so neither was performed.

## Project summary and release authorization

Added Docs/ProjectOverview.zh-CN.md covering verified design, finite-scale boundaries, actual CC0/procedural assets,
zero generation spend, native route evidence and editor handoff. The user subsequently explicitly authorized merging
into main and publishing the version tag; this supersedes the earlier missing-authorization block.
