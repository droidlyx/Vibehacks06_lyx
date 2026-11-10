# NPC travel failure: runtime evidence

The resource-policy comparison exposed a resident selecting the highland grove but failing to arrive. This was not counted as a successful world-effect test.

A native LLDB session attached to the new game's UnrealEditor process through Rider. It paused at VerdantNPC.cpp line77, inside the branch that stops movement after forward and both side probes are blocked. Stack: AVerdantCharacter::MoveToward -> AActor::TickActor -> FActorTickFunction::ExecuteTick -> task graph.

Observed values:

- AgentId2 / 陶恩; HomeGrove2; HostileId-1; ActionLabel: 前往 高地果林.
- Position (2257.89,2370.19,224.74); destination (429.31,6281.13,147.19).
- BodySize1.2917; capsule radius41.34cm; probe sphere radius31.00cm.
- Forward blocking hit distance109.99cm; the final side hit distance43.25cm.
- Both hit component Woodland_14, whose actual mesh is SM_Tree02.
- Tree component origin (2547.57,2469.22,113.28), approximately306cm horizontally from the character; impact normal z=-0.7071.

The imported tree's whole-mesh collision blocked space far from its trunk. Short left/right steering then chose to stop repeatedly. The minimal fix disables collision on native tree visuals and supplies a separate trunk capsule; foliage appearance and positions are unchanged. This also keeps the solution consistent with the already separate Mother Tree trunk collider.

The agent breakpoint was removed and the debugging session stopped; the original9 user exception breakpoints were left unchanged. The debugging run's policy output is retained locally as policy-debug-open.json and is not used as a deterministic comparison. Fresh deterministic closed/open runs validate the fix.
