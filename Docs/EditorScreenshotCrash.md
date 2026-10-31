# Editor screenshot crash — 2026-10-31

## Finding

The latest crash is in Slate's screenshot readback, not in the fractal generator's call
stack. A failed Rider MCP viewport capture is the leading trigger, supported by the
sequence of events and the installed source. The exact buffer address/lifetime has not
been verified in a debugger or reproduced; do not describe this as a completed binary fix.

## Evidence

- The editor loaded `UnrealEditor-Verdant-1675.dll`. Two PIE runs at 02:13:37 and
  02:14:05 UTC successfully generated the new fractal mesh: 641,601 vertices,
  1,280,000 triangles, 8 octaves, 0.5 m spacing, heights -6.88 to 71.86 m.
- PIE ended at 02:14:12 UTC. At approximately 02:19:20 UTC the agent inspected the
  idle editor and requested an MCP viewport screenshot. The tool returned
  `FSlateApplication::TakeScreenshot failed for viewport`.
- At 02:27:56 UTC (10:27:56 China time), the render thread crashed writing address
  zero in `ConvertDXGIToFColor`, called by `RHIReadSurfaceData` from Slate's
  `DrawWindowViewport_RenderThread` screenshot readback lambda. The breadcrumb is
  `SlateUI / RenderGraphExecute - Slate`.
- Preserved crash evidence:
  `Saved/Crashes/UECC-Windows-D6A1E1814E38F12923C0878E9F2544BE_0000/`.
  Read its `Verdant.log`, `CrashContext.runtime-xml`, and `UEMinidump.dmp` locally;
  do not publish account identifiers from the crash context.

## Source-level mechanism

Installed RiderLink 2026.2.3.1:
`E:/UE_5.8/Engine/Plugins/Marketplace/Developer/RiderLink/Source/RiderAgentTools/Private/ScreenshotCapturer.cpp`.

`CaptureViewport` creates a stack-local `TArray<FColor> Pixels` and passes it to
`CaptureWidget`. `CaptureWidget` immediately returns on `TakeScreenshot` failure,
without cancelling the renderer's pending screenshot request.

In local UE source, `FSlateApplication::TakeScreenshot` (SlateApplication.cpp:4429)
registers the output array with the renderer before attempting to draw the window.
`FSlateRHIRenderer::PrepareToTakeScreenshot` (SlateRHIRenderer.cpp:1409) stores the
array's raw pointer in `ScreenshotState`. `DrawWindows_Private` clears this state only
when the requested viewport is among the windows being rendered (lines 1716–1760).
A hidden/minimized or otherwise non-rendering target can therefore return capture
failure with the request still pending. On later rendering, the readback lambda uses
that pointer (lines 1195–1206), after the caller's array may have been destroyed.

Window minimization is a plausible condition, not directly established by the log.
This also explains why failure and crash need not happen on the same frame.

## Immediate mitigation

1. Stop using Rider MCP `take_screenshot` for editor windows and viewports. The agent
   has recorded this in the project's `AGENTS.md`; the plugin itself is not disabled.
2. Reopen the editor after the crash. A new process clears pending screenshot state.
   No project terrain rollback, cache deletion, driver reinstall or RHI change is
   justified by the available evidence.
3. Continue ordinary editing and Play manually; inspect logs/properties through MCP
   without screenshot capture. Do not deliberately reproduce the crash.

## Proposed source fix (not installed)

`Tools/Patches/RiderLink-screenshot-lifetime.patch` adds two protections to the
plugin: reject hidden/minimized/unattached windows before queuing a capture, and
after every attempted capture, flush submitted render work and detach any unconsumed
request from the target viewport before the caller's pixel array goes out of scope.

UE 5.8 has no cancellation method on `FSlateRenderer`; the proposed detachment uses
`PrepareToTakeScreenshot` with an empty rectangle, a non-null array as required by
the API, and a null target window. This is source-reviewed against this engine's
`WindowToViewportInfo.FindRef` implementation, but is not a universally portable API
contract. The enduring engine fix would expose an explicit cancellation/lifetime-safe
screenshot API and clean up failed capture state inside Slate itself.

The patch is saved for review. It has NOT been applied to the engine-wide RiderLink
installation, compiled, or tested. Applying source alone would not change the loaded
plugin binary. Rebuild the patched plugin with the editor closed before using it.

## Separate earlier failure

The 02:10 UTC crash was a different failure: legacy Hot Reload began while a failed
Live Coding reload remained active, triggering `GActiveReloadInterface == nullptr`.
Do not conflate it with this screenshot readback crash, and do not switch reload modes
mid-failure as a recovery mechanism.
