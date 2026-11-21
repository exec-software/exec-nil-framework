# NIL Framework SDK 1.0.0-rc.1

This is a release candidate for the first stable NIL Framework SDK.

## What's included

- **Compiler** — Ny/Matrix bytecode compiler with LLVM backend
- **Standard Library** — 16 modules covering I/O, collections, database providers, and declaration reflection
- **Runtime** — Static and dynamic runtime libraries
- **Interpreter** — Bytecode interpreter with JIT/AOT support
- **Agent tools** — `nilagent` for workspace management and bounded execution
- **Build system** — CMake-based with Nora project management integration

## Platforms

| Platform | Architecture | Asset |
|----------|-------------|-------|
| Linux | x86_64 | `nil-sdk-v1.0.0-rc.1-linux-x86_64.tar.gz` |
| macOS | arm64 | `nil-sdk-v1.0.0-rc.1-macos-arm64.tar.gz` |
| macOS | x86_64 | `nil-sdk-v1.0.0-rc.1-macos-x86_64.tar.gz` |

## Installation

```sh
# Extract to your desired location
tar xzf nil-sdk-v1.0.0-rc.1-linux-x86_64.tar.gz -C /opt/nil-framework

# Set environment
export NIL_SDK_DIR=/opt/nil-framework/sdk
export PATH="$NIL_SDK_DIR/bin:$PATH"
```

## Verification

```sh
# Verify SHA256 checksums
sha256sum -c SHA256SUMS

# Check SDK manifest
nil sdk verify
```

## Notes

- This is a release candidate. See [release/README.md](https://github.com/exec-software/exec-nil-framework/blob/main/release/README.md) for the preparation status.
- The Linux SDK is built with the rc-checks qualification profile.
- The macOS SDKs are development candidates.
- Nothing is signed or activated by this release process.
