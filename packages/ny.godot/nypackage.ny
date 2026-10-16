@pragma once
# =============================================================================
# nypackage.ny — Package manifest for ny.godot
# =============================================================================
# This file is the authoritative package descriptor, similar to:
#   NuGet   → .csproj / .nuspec
#   Cargo   → Cargo.toml
#   npm     → package.json
#   pip     → pyproject.toml
#
# nypm (Ny Package Manager) reads this file to:
#   - Resolve the package identity and version
#   - Download / link native binaries
#   - Set up the include path for the Ny compiler
#   - Satisfy dependency constraints
# =============================================================================

package ny.godot

# ── Identity ──────────────────────────────────────────────────────────────────

pub const PACKAGE_ID      = "ny.godot"
pub const VERSION         = "0.1.0"
pub const MIN_NIL_VERSION  = "0.1.0"
pub const DESCRIPTION     = "Experimental Godot 4.x bindings for Ny. Provides math, node, scene, and input APIs for a future GDExtension bridge."
pub const AUTHORS         = ["Execute Software"]
pub const LICENSE         = "MIT"
pub const REPOSITORY      = "https://github.com/execute-software/exec-ny-lang-packages"
pub const HOMEPAGE        = "https://packages.ny-lang.org/ny.godot"
pub const KEYWORDS        = ["godot", "game-engine", "gdextension", "bindings"]

# ── Dependencies ──────────────────────────────────────────────────────────────

dependencies:
  - id: "ny.lang.core"
    version: ">=0.1.0"
    optional: false

# ── Source files ──────────────────────────────────────────────────────────────
# Listed in compilation order. The package manager feeds these to nilcompiler
# in order when building a project that imports ny.godot.

sources:
  - src/math.ny
  - src/node.ny
  - src/scene.ny
  - src/input.ny
  - src/package.ny

# ── Native binaries ───────────────────────────────────────────────────────────
# These are the intended artifact locations once the native bridge is shipped.
# No native binaries or native build project currently live in this repository.

native:
  windows_x64: "native/bin/ml_godot.windows.x86_64.dll"
  linux_x64:   "native/bin/ml_godot.linux.x86_64.so"
  macos:       "native/bin/ml_godot.macos.framework"

# ── Build instructions for the native layer ───────────────────────────────────
# Proposed build metadata. The referenced native project is external and nypm
# is not yet implemented in this repository.

native_build:
  source_dir: "native/src"
  cmake:      "native/CMakeLists.txt"
  requires:
    - "exec-gehenna-engine-sdk"   # godot-cpp (the SDK repo)
    - "cmake >= 3.22"
    - "scons"
