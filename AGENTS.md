# Editor automation safeguards

## Known screenshot crash (2026-10-31)

Do not call Rider MCP `take_screenshot` with `kind=viewport` or `kind=editor_window`
in this project until the installed RiderLink/Slate capture fix is explicitly verified.
A failed capture can leave a deferred Slate screenshot request referencing a destroyed
temporary pixel array. Rendering the window later can crash in `ConvertDXGIToFColor`.
Do not retry a failed capture, even if it initially appears harmless.

Use MCP Python for targeted scene/property inspection and read existing logs or saved
images. Do not reproduce the crash as a test. The user has asked to do gameplay testing
themselves; do not run unsolicited broad test suites.

Read `Docs/EditorScreenshotCrash.md` for evidence, mitigation and patch status.
The patch under `Tools/Patches` is a proposal, NOT an installed or validated engine fix.

## Hot reload state

If Live Coding fails (for example because the loaded DLL has no matching PDB), do not
start legacy Hot Reload in the same session while the Live Coding compile/reload is
still active. Disabling its settings checkbox does not cancel the active reload state.
This previously triggered `GActiveReloadInterface == nullptr` and exited the editor.
Report the failure and arrange a clean rebuild/restart rather than stacking reloads.
