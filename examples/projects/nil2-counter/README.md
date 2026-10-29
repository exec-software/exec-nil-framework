# NIL² counter and settings

A Matrix desktop application built with `nil.std.ui`. It has a live greeting,
a text entry, a checkbox that selects increments of one or ten, and increment,
reset and close buttons. The controls use native GTK4 layouts and events.

With a current NIL SDK on `PATH`, run from this directory:

```sh
nilbuild run
nilbuild run --target native
```

For tools built from a source checkout, supply that checkout as the SDK:

```sh
/path/to/exec-nil-framework/build/cmake/bin/nilbuild run --sdk /path/to/exec-nil-framework
/path/to/exec-nil-framework/build/cmake/bin/nilbuild run --sdk /path/to/exec-nil-framework --target native
```

The host needs Linux, GTK4 and a working graphical session. Native compilation
also needs an LLVM-enabled compiler and the host linker. Missing provider or
display support produces an explanatory error.

The window starts hidden while its controls are constructed. The event loop
copies queued events, updates explicit `TextModel` and `FlagModel` state, and
refreshes dependent controls only after a change. Programmatic setters suppress
feedback events. Clicking Close or using the window manager exits the loop and
closes the window.

The example displays the framework generation as **NIL²**, with **NIL^2** as
the plain-text spelling; this does not change its numeric release identity.
See the [UI API and lifecycle](../../../docs/standard-library/ui.md) and
[project workflow](../../../docs/tools/projects.md) for details.
