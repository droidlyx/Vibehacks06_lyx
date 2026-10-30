# Native CSV-capture exit anomaly

The first native full-journey run reached100 nutrition and passed all11 gameplay checks, then exited with777003 after writing its normal shutdown log. It was retained as a failed process run, not a clean validation.

The installed UE5.8.3 source maps777003 to ECrashExitCodes::CrashReporterCrashed: the SEH fallback in FCrashReportingThread::Run (WindowsPlatformCrashContext.cpp:1345). This identifies the reporting-thread fallback, not necessarily the original exception.

A Rider/LLDB attach to the same native executable succeeded. The engine-source breakpoint reported no matching executable code, so no paused engine values or original exception address were captured. The session ended; the agent breakpoint was removed, leaving the original9 user exception breakpoints unchanged. This is a precise debugger-symbol limitation, not a claim that the engine internals were repaired.

## Controlled comparisons

All use the same native executable and1280x720 offscreen rendering, with actual process exit codes retained:

| Run | Instrumentation | Exit |
|---|---|---|
| Full journey | csvCaptureFrames=3600 + csvGpuStats |777003|
| Smoke | csvCaptureFrames=2000 + csvGpuStats |777003|
| Smoke | csvCaptureFrames=2000 only |777003|
| Smoke | csvGpuStats only |0|
| Full journey | None |0|

The tested trigger is native boot CSV capture. The exact internal shutdown fault remains unproven. The regular Play.cmd launcher never enables this option. Formal gameplay validation now runs without instrumentation, keeps nonzero exits as failures, and does not disable crash handling. The separate performance capture retains its abnormal-exit qualification; it is not counted as a successful gameplay run.

This limitation applies to this engine/tooling combination and these observations, not a claim about every Unreal build or hardware configuration. No engine binaries or global engine settings were patched.
