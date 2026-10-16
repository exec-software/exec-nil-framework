# `ny.godot`

`ny.godot` is an experimental set of Ny declarations for a future Godot 4
GDExtension bridge. The active source set currently covers math types, node
lifecycle operations, scene access, and input.

This repository does not contain a native Godot bridge or a working `nypm`
client, so the package is not currently installable or runnable by itself.
Several declarations still name the legacy `ml_godot` ABI; a future bridge
must introduce canonical `ny_godot` exports with compatibility aliases.
`nypackage.ny` records the intended package metadata and artifact layout.

The older `ny.nullptr.*` adapter drafts are retained under
`experimental/ny.nullptr/`. They are not exported by `ny.godot` or listed in
its active source manifest.
