@pragma once
# =============================================================================
# exec-ny-lang-packages / registry / packages.ny
# =============================================================================
# Package index for the Ny package registry.
# The ny package manager (nypm) reads this to resolve package names → source.
# Add a new entry here when publishing a new package.
# =============================================================================

package nypm.registry

pub const REGISTRY_VERSION = "1"
pub const REGISTRY_URL     = "https://packages.ny-lang.org"

# Format:
#   PackageEntry { id, version, description, native_required, path }

pub const PACKAGES = [
    PackageEntry {
        id:              "ny.godot",
        version:         "0.1.0",
        description:     "Godot 4.x engine bindings — node lifecycle, math, input, physics, audio, UI",
        native_required: true,
        path:            "ny.godot",
        tags:            ["godot", "game", "engine", "gdextension"]
    },
]

pub struct PackageEntry
    id:              text
    version:         text
    description:     text
    native_required: bool
    path:            text
    tags:            [text]
end struct
