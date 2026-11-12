# NIL-0025: Matrix desktop UI

- **Status:** Implemented GTK4 on Linux, Win32 on Windows and AppKit on Intel/Apple Silicon macOS; release qualification remains separate

The original Linux design below and dated Windows GTK addendum are retained as
design history. The current provider addendum supersedes that Windows deployment.
Current platform evidence and release limits are summarized in
[platform support](../platform-support.md).

## Purpose

Provide a standard application UI library authored in Matrix, usable from Ny
and Matrix through the same VM and native runtime. The public library and
provider code use the repository's MIT license. GTK remains an optional system
dependency under its own license; the SDK does not redistribute it.

The earlier `packages/ny.ui.mimic` and `nilaix` experiments are not the
implementation base. Their simulated or incomplete providers cannot establish
desktop behavior. The new library lives under `nilstd/src/ui` with the public
identity `nil.std.ui`.

## Boundaries

Matrix owns typed Window, Widget, EventKind and Event values, constructors,
checked operations, event decoding, and text/flag model synchronization. A
small versioned `ui.v1.*` intrinsic boundary carries only Boolean, integer,
text, and the standard Result type. The checker, VM and LLVM backend validate
this ABI; a similarly named user enum is not an interchangeable Result.

The shared C++ runtime loads GTK 4 through its public opaque-pointer ABI on
Linux. Loading is optional and does not require toolkit development headers.
It never fabricates successful windows when a provider or display is missing.
Creating a window returns Result.Error with the reason in that case.

GTK callbacks enqueue copied events; they do not invoke arbitrary language
function pointers. `poll-event` returns Result of Option of Event: absence is
an empty queue, and failure is an error. Close requests are events which the
application can accept by explicitly closing the window. Programmatic setters
suppress change events, so explicit model synchronization cannot recursively
feed itself. This boundary leaves room for a future checked callback API.

## Resources

Each runtime context owns its window tree and snapshots. Handles are checked
identities, not addresses. Closing a window invalidates its children and queued
snapshots; cross-context and stale handles fail. Snapshot data is copied into
Matrix values and released even when decoding fails. VM completion, faults,
reset and native invocation completion release the context's resources.

The provider enforces its creation thread and one GTK thread per process.
Widget, window, text, event and snapshot counts are bounded. Text is valid UTF-8
without embedded NUL; Boolean properties accept only zero or one. Grid
placement rejects overlap and invalid parentage. Application code owns the
event loop and can use ordinary runtime timing primitives between polls.

## Verification and scope

Pure models have co-located niltest cases. Provider tests cover error paths,
resource lifetimes and toolkit state. An isolated authenticated Xvfb display
hosts the actual GTK windows for VM/native tests. XTest drives entry typing and
keyboard activation; a window-manager close message verifies close requests.
The harness never targets the developer's desktop. Missing Xvfb is an explicit
test skip, not desktop evidence.

The initial controls are row/column/grid layouts, labels, buttons, entries and
checkboxes, with common sizing, spacing, margins, visibility and sensitivity.
This is a foundation for application development. Declarative NILData views,
menus, dialogs, tree/table models, styling, richer bindings, accessibility
audits, application packaging and additional OS providers remain follow-up
work. GTK supplies native text rendering and control behavior; those features
must receive focused evidence before broader cross-platform claims.

## Public version presentation

Examples and public UI may display the generation as **NIL²**, followed by
NIL³, NIL⁴ and later generations. Plain-text surfaces can use `NIL^2`.
This presentation is separate from the Execute-format numeric release and
artifact identity in `VERSION`; this change does not cut or renumber a release.

## Windows development addendum — 2026-11-02

The original Linux profile above now has a Windows development implementation
using the same checked provider API. GTK4 remains an external dependency:
`NIL_UI_GTK_DIRECTORY` selects an absolute runtime DLL directory, with
executable-adjacent deployment as the alternative. The provider does not search
the current directory or `PATH`, change DLL search policy, or install GTK.
Window creation requires an interactive, visible window station; noninteractive
service/SSH requests fail before GTK initialization.

The approved Windows desktop run passed the provider/lifecycle checks and all
10 desktop/interaction VM/native cases. Its harness targeted only verified HWNDs
whose retained process identities belonged to private Jobs, without global
input injection, focus changes or desktop switching. This is development
evidence, not Windows release, bare-host or broader accessibility qualification.
See the [current UI deployment and test contract](../standard-library/ui.md)
and [Windows validation notebook](../../notebook/2026-11-02-windows-port.md)
for the supported environment, evidence and remaining boundaries.

## Current native providers — 2026-11-11

The checked `nil.std.ui` API now uses GTK4 only on Linux. Windows uses Win32
directly, with no external GTK runtime or DLL-directory setting. macOS uses
AppKit/Cocoa and CoreGraphics, with creation and event processing on the process
main thread. The debugger keeps VM execution and UI cleanup on that same thread.
Win32 and AppKit implement row/column/grid layout behind the common boundary,
including hidden-child reflow, grid spans, expansion and fill.

Both installed library roots pass ten interpreter/native desktop and input
cases on Windows and each Mac architecture. Tests use verified owned windows
on Windows and in-process AppKit responders on macOS. These checks supplement
the provider/lifecycle tests; they do not establish arbitrary desktop automation,
accessibility support, bare-host installation or release qualification.
See the [UI contract](../standard-library/ui.md) and
[platform matrix](../platform-support.md) for the current deployment requirements
and separately recorded editor acceptance.
