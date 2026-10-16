# First-party Ny packages

This directory contains opt-in packages that are developed alongside Ny but
are not part of the core `ny.lang.*` standard library.

| Package | Purpose | Status |
| --- | --- | --- |
| `ny.engine` | Rendering, ECS, physics, input, and engine experiments | Experimental |
| `ny.godot` | Godot binding declarations and proposed package metadata | Experimental |
| `ny.ui.mimic` | Cross-platform UI toolkit experiments | Experimental |
| `registry` | Prototype package-registry data | Experimental |

Each package owns its source and experimental areas. Shipping, native-library,
and package-manager integration remain future work unless a package says
otherwise in its own documentation.
