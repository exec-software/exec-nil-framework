# SDK Installer

A Visual Studio–inspired modular installer for the NIL framework, written in Matrix.

## Overview

The NIL framework contains a lot of moving parts. Not everyone needs all the
moving parts, but there is a "core" workload. This installer lets users
modularly install the extras they need, skip what they don't, change the
install location for the components, set up environment variables, and build
any initial files required.

## Architecture

The installer is written in Matrix and uses the `nilstd` standard library.
Configuration is stored in [NILData](https://example.com/nildata) (`.nildata`)
files, consistent with the rest of the framework.

### Modules

- `src/config/installer-config.matrix` — Installer configuration model and
  persistence (workloads, install location, selected components).
- `src/installer/workload.matrix` — Workload definitions: core + modular
  extras (ui, security, collections extras, etc.).
- `src/installer/component.matrix` — Component model: what a component installs,
  its size, dependencies, and the files it contributes.
- `src/installer/installer.matrix` — The installer engine: resolves a workload
  + selection, computes the file plan, writes files, sets up environment
  variables, and writes the install manifest.
- `src/installer/env-setup.matrix` — Environment variable management: sets
  `NIL_SDK_PATH`, `PATH`, and workspace defaults.
- `src/installer/ui-wizard.matrix` — Interactive selection UI (when the UI
  provider is available) with a console fallback.
- `src/main.matrix` — Entry point: parses CLI arguments, loads config, runs
  the wizard or installs from a saved selection, writes the manifest.

### Project Manifest

The project uses a `nil.project.nildata` manifest so the installer itself can
be built with `nilbuild`.

### Configuration

The installer reads `install-config.nildata` from the framework root (or a
path specified on the command line). This file describes:

- Available workloads (core, ui-dev, security-research, full)
- Components within each workload
- Default install location
- Environment variables to set

After installation, the installer writes `install-manifest.nildata` to the
install location, recording what was installed and from which SDK version.

## Workload Model

Inspired by the Visual Studio installer, workloads are named sets of
components. The "core" workload is always available and includes the bare
minimum to compile and run NIL programs (the compiler, interpreter, nilbuild,
nilstd core). Extras are optional modules that can be toggled independently.

### Components

Each component has:
- A unique ID
- A human-readable name and description
- A size estimate (bytes)
- Dependencies on other components
- The set of files it contributes (relative to the SDK root)
- An optional environment variable hook

### Resolution

Given a set of selected workloads, the installer computes the transitive
closure of components, checks for conflicts, and produces a file plan. Files
from multiple components are de-duplicated.

## Usage

```sh
# Build the installer (from the project directory)
nilbuild run

# Run interactively (console wizard)
./build/sdkinstaller --interactive

# Run with the UI provider (if available)
./build/sdkinstaller --ui

# Install a named workload to a custom location
./build/sdkinstaller --workload core --location /opt/nil

# Install with specific components selected
./build/sdkinstaller --workload ui-dev --without core.testing

# Show what would be installed without writing anything
./build/sdkinstaller --workload security-research --dry-run
```

## Environment Setup

After installation, the installer writes:

- `nil.env` — Environment variable exports (sourced by shells)
- `sdkinstaller-state.nildata` — Persistent state for the SDK location, so
  `nilbuild` can find the SDK without explicit `--sdk`

Environment variables set:
- `NIL_SDK_PATH` — Absolute path to the installed SDK root
- `PATH` — Prepends the SDK's `bin/` directory
