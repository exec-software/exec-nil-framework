# NIL² desktop UI

`read nil.std.ui` provides a Matrix desktop library with typed windows, widgets,
events and explicit model bindings. Ny programs can import the same module.
The VM and native compiler use one desktop provider per platform, each behind
the same checked boundary: a GTK4 provider on Linux and a Win32 provider on
Windows. Windows remains a development target, not a qualified release platform.
The macOS provider is AppKit. Its provider, lifecycle and interpreter/native
language-level desktop contracts pass on Intel and Apple Silicon; current SDK
and editor acceptance remain separate. AppKit replaces the
earlier private GTK-on-macOS experiments. See [platform support](../platform-support.md).
Examples display the framework generation as **NIL²**, with **NIL^2** as the
plain-text spelling. This presentation does not change numeric release or
artifact identities in `VERSION`.

This first version supports real windows, row and column layouts, grids,
labels, buttons, text entries and checkboxes. Application logic stays in Matrix;
the provider implements widget operations and copies events across the runtime
boundary. It does not call application code from GTK callbacks.

## Run the example

The [counter and settings project](../../examples/projects/nil2-counter/README.md)
contains a complete event loop. Editing the name updates its greeting, the
checkbox changes the increment from one to ten, and the buttons increment,
reset and close the application.

With a current SDK on `PATH`, run from `examples/projects/nil2-counter`:

```sh
nilbuild run
nilbuild run --target native
```

For a source checkout, pass `--sdk /absolute/path/to/exec-nil-framework` and use
the tools from that checkout's build. See the [project workflow](../tools/projects.md)
for compiler selection. Native output requires an LLVM-enabled compiler and
the host linker.

The Linux host must provide GTK4 and a working graphical session, through
X11 or Wayland. `available with nothing` checks whether the provider's required
symbols can load; it does not connect to a display. Window creation returns
`Result.Error` if the graphical session cannot be initialized. There is no
headless widget simulation or automatic package installation.

Windows needs no external toolkit. The provider is Win32 itself, so
`available with nothing` is always true there and no runtime has to be deployed
or configured; GTK is neither used nor loaded on Windows. Because USER32 and
GDI32 are called directly rather than loaded on demand, every linked program
imports them, which is why `user32.lib` and `gdi32.lib` join `ws2_32.lib` in the
Windows link lines and in the incremental build's toolchain identity.

Windows window creation still requires an interactive, visible window station.
A service session, or an SSH session with no desktop, returns a display error
before any window is created; successful provider availability alone does not
prove a desktop is usable. Win32 supplies no box or grid container, so the
provider owns the layout pass for rows, columns and grids; sizes therefore come
from measuring text with the system message font rather than from the toolkit.

macOS needs no external toolkit either. The provider is AppKit, so
`available with nothing` is always true there and GTK is neither used nor
loaded; `NIL_UI_GTK_DIRECTORY` has no meaning on macOS. Window creation
requires a window server session and returns a display error without one.
Because the provider calls Cocoa and CoreGraphics directly, every linked
program carries those frameworks, which is why they appear in the Darwin link
lines alongside the runtime.

GTK is no longer a macOS provider. The earlier private GTK profile failed its
cross-process accessibility probes; those observations do not qualify every
possible GTK configuration. AppKit is the supported source implementation.

On macOS, creation and polling must run on the process main thread, which is
where AppKit requires its windows to be used. Ordinary interpreter, JIT and
native entry routes use the main thread. The LLVM-ON display campaign verifies
the interpreter and native application cases on both architectures. DAP now
keeps the VM and its UI resources on the process main thread across source
stops. Its real GTK, Win32 and AppKit ownership/event tests pass, including
retained handles and queued events across source steps and disconnect cleanup.
Paused debug sessions do not automatically pump window events. AppKit
supplies no box or grid container, so, as on Win32, the provider owns the
layout pass and sizes come from each control's fitting size.

Hidden children reserve no layout space. A grid span preserves the child's
full requested size, and expanding tracks share available space. Containers
inherit visible descendants' expansion requests unless explicitly overridden;
cross-axis fill is independent of requesting extra major-axis space. Win32 and
AppKit share bounded integer allocation, cached measurement and iterative
placement. Native geometry assertions compare these behaviors against GTK
without requiring identical fonts or pixel measurements.

## Windows and widgets

`Window` and `Widget` are distinct records containing checked provider handles.
Use constructors to obtain them. Operations validate context ownership, kind
and lifetime; a fabricated, closed or foreign handle returns an error.
Copying a record refers to the same underlying object and does not create a
second window or widget.

| Operation | Result and behavior |
| --- | --- |
| `create-window(title, width, height)` | `Result<Window,text>`; creates a hidden window. |
| `window-root(window)` | `Widget`; the window's automatic column container. |
| `create-column(parent)`, `create-row(parent)`, `create-grid(parent)` | `Result<Widget,text>`; nested layout containers. |
| `create-label(parent, text)`, `create-button(parent, text)` | `Result<Widget,text>`; labeled controls. |
| `create-entry(parent, text)`, `create-checkbox(parent, text)` | `Result<Widget,text>`; initial entry content or checkbox label. A new checkbox is unchecked. |
| `show-window(window)`, `close-window(window)` | `Result<boolean,text>`; present or destroy the window. |
| `set-widget-text(widget, text)`, `widget-text(widget)` | Write or read label/entry text; reading returns `Result<text,text>`. |
| `set-checked(widget, boolean)`, `widget-checked(widget)` | Checkbox state; reading returns `Result<boolean,text>`. |
| `set-sensitive(widget, boolean)`, `set-visible(widget, boolean)` | Enable/disable interaction or show/hide a widget. |
| `set-margin(widget, pixels)`, `set-spacing(container, pixels)` | Equal outer margins or spacing between children. For the window root, affect its automatic column. |
| `set-expansion(widget, horizontal, vertical)` | Set the two layout expansion flags. The writes are sequential, not transactional. |
| `attach-grid(grid, child, column, row, column-span, row-span)` | Reposition an existing child of that grid; it cannot reparent a widget. |
| `focus-widget(widget)` | Request keyboard focus, normally after showing the window. |
| `activate-widget(widget)` | Request the provider's real activation of an enabled button or checkbox. |
| `widget-width(widget)`, `widget-height(widget)` | `Result<number,text>`; current allocation in pixels, which can be zero before layout. |

Mutation operations return `Result<boolean,text>` with `yes` on success unless
a model synchronization operation explicitly reports that no write was needed.
Inappropriate properties return errors: for example, a label has no checked
state, and a button cannot be a parent container.

Grid children initially occupy successive rows in column zero, each spanning
one cell. `attach-grid` validates the new rectangle against existing child
rectangles. When rearranging a grid, choose an order that does not temporarily
overlap another child.

Closing a window invalidates its children and queued provider events. Calling
close again is an error. Runtime teardown closes any remaining windows, but
applications should close windows explicitly when they are finished.

## Events

`poll-event(surface)` returns `Result<Option<Event>,text>`. The public
`PendingEvent` alias names `Option<Event>`, so Matrix spells this return type
`Result of PendingEvent, text`. `Option.None` means
the queue is currently empty. `Result.Error` reports a provider or resource
failure. No public null or sentinel handle represents an empty queue.

An `Event` contains `kind`, `window`, `target`, `content` and `checked`:

| Kind | Target | Event data |
| --- | --- | --- |
| `EventKind.Clicked` | `Option.Some` containing the button | Empty content. |
| `EventKind.TextChanged` | `Option.Some` containing the entry | A copy of the new text in `content`. |
| `EventKind.Toggled` | `Option.Some` containing the checkbox | The new Boolean state in `checked`. |
| `EventKind.CloseRequested` | `Option.None` | A window-manager close request. The window remains open until `close-window` is called. |

Use `event-for(event, widget)` to match a target, and `is-click`,
`is-text-change`, `is-toggle` or `is-close-request` to test the kind. `content`
is empty for other event kinds; `checked` is `no` for kinds other than a toggle.
Fields are copied before the provider snapshot is released, including cleanup
when a field cannot be read. A retained event therefore describes the original
event even after another mutation or window close. Its copied target handle
can still become stale.

Polling is nonblocking and processes a bounded batch of provider work. An ordinary
event loop sleeps briefly when it receives `Option.None`; the example uses
16 milliseconds. Call UI operations from the context's creation thread and use
one UI thread in the process. Worker jobs should return application data for
that thread to apply.

## Explicit model bindings

`create-text-model(initial)` constructs a `TextModel` with a `content` field;
`create-flag-model(initial)` constructs a `FlagModel` with an `enabled` field.
Records share identity when assigned. Construct another model for independent
state.

| Operation | Behavior |
| --- | --- |
| `update-text-model(model, next-value)`, `update-flag-model(model, next-value)` | Update model state and return whether it changed. |
| `sync-text(widget, model)`, `sync-flag(widget, model)` | Read the widget, write only if different, and return `Result<boolean,text>` indicating whether a write was needed. |
| `apply-text-event(model, widget, event)`, `apply-flag-event(model, widget, event)` | Update the model only for the matching widget and event kind; return whether the model changed. |

Programmatic text and checked-state setters suppress feedback events. Model
synchronization also compares values before writing. The application decides
when to synchronize and which dependent views to refresh; this is not an
automatic subscription or asynchronous command system.

## Bounds and errors

The provider validates text as NUL-free UTF-8. A value is limited to 1 MiB;
entries additionally accept at most 16,384 Unicode scalar values. Window
dimensions are 1–16,384 pixels, margins and spacing are 0–4,096 pixels, and grid
coordinates and extents must remain within the provider's 0–65,535 range.

Each context allows 64 windows and 4,096 registered nodes, including windows.
It bounds queued events and outstanding snapshots at 4,096 each, with separate
16 MiB budgets for retained labels and event text. Queue overflow is reported
as an error rather than silently presenting a complete event stream. Handle
validation and these limits apply equally to VM and native execution.

Errors describe the failed operation or violated bound. Handle errors do not
grant access to another context's objects. GTK supplies system styling, fonts
and baseline accessibility behavior. Custom drawing, menus and dialogs,
virtualized views, a visual designer, and providers for other operating systems
are outside this initial release.

## Testing

The pure model tests are colocated in `nilstd/src/ui/models.matrix` and need no
display. The `nil.std.ui.gtk.desktop` suite in `tests/ui/gtk_desktop.matrix`
exercises real controls, layouts, bindings, immutable event copies, errors and
closed handles. The separate `nil.std.ui.gtk.interaction` suite checks actual
keyboard input and a window-manager close request.

`nil.ui-display` runs the desktop suites through both routes when native
compilation is enabled. On Linux it uses a private Xvfb display and X11 input
driver, without sending input to an existing desktop session. On Windows its
separate harness requires an existing interactive desktop, and posts input only
to verified test windows belonging to its private process Job. It does not use
global input injection, change focus or switch desktops. Desktop interaction cases
run sequentially with `niltest --jobs 1`; see [compiler-backed testing](../tools/testing.md)
for filtering, reports and timeouts.

The macOS harness uses a test-only library inside each owning process to send
input through its verified AppKit responder chain. It exercises the same Matrix
interaction source and records process-bound evidence without Accessibility
permissions or global input. This library is not installed in the SDK. Hosted
qualification of this new language-level input path is still pending.

The same display harness checks real GTK window counts during VM pauses and
after completion, faults, resets, reloads and destruction. Native context
completion/restart releases windows, while copied text remains valid across
managed collection. CI installs the optional GTK/Xvfb dependencies so desktop
coverage is required there; local builds without them report a test skip.
The Windows harness's `--require-display` mode turns a missing desktop into a
qualification failure. Optional headless mode checks an explicitly configured
desktop before allowing a skip; any failure after starting a display fixture
remains a failure. A headless skip is not desktop qualification. Linux loader
probes separately cover GTK library selection and rejection before initialization.
