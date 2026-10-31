# v0.3: choices that leave different worlds

## New decisions

- B spends up to5 actual food on nearby permitted soil, improving fertility by3 percentage points per food. Fully fertile soil costs nothing. This real public help also builds bargaining credibility.
- Residents protect different food reserves (6/12/18), remember actual net gifts, and match a bounded share of actual player donations/tending. Repeating a request or promising future help does not create credit.
- A peaceful cession transfers one real grove while its keeper stays alive. G cycles free access,30% real collection share, and restricted access on a grove you manage.
- Following is earned through deeds, gifts or size. Companions defend their leader, avoid their party, preserve the agreement after outside attacks, withdraw from stronger threats and recover with real food. Betrayal by the leader ends the agreement.
- Residents can spend their own surplus to tend poor permitted soil on site, at most once per60 seconds.

## Feedback and robustness

- Live Chinese world labels use a runtime UFont; the height ruler compares your current body against the initial81cm. Attack direction arcs and real damage numbers make timing readable.
- One late attack click is buffered during recovery, then consumed once; dialogue, stagger and dodge cancel it.
- Conversations have separate visible histories and statuses. A delayed response cannot appear as a different resident's reply. Model-visible history is bounded by complete exchanges and a2000-character budget.
- The model sees the same legal action/target/amount list as the executor. Refusal cannot be silently substituted with closing a grove. Action status states what actually occurred, separately from NPC speech.

## Observed routes (editor and native verification)

Both ordinary-resource routes use actual movement, collection, tending and real local-model decisions. No teleports, free food/size or frozen residents are used.

- Public route:3 freely accessible groves, all3 main residents alive, no player-owned grove.
- Steward route:2 player-managed groves collecting real30% shares, all3 main residents alive; highland remains restricted.

An earlier steward attempt obtained only one grove because the actual help was below the second keeper's threshold. Its failed outcome is retained. The next route performs additional real care and offering; it does not relax the eligibility rules.

Two independent600-second natural simulations checked72014 frames in total, with zero numeric-invariant failures and zero stationary-travel suspects. With no player pressure both worlds eventually settle into stocked, mostly waiting residents; this is stability evidence, not a claim of endless self-generated drama. The open variant changes one owner policy at2 seconds and is identified as an intervention.

All tools and game/editor validation remain hidden and muted for this user-requested two-hour iteration. The native v0.3 package passed all eight validation cases, each with exit code 0 and a fresh zero-failure report. Evidence: Docs/Checks/v03-native. Public and steward routes both reached 100 nutrition through normal resources.
