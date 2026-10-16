@pragma once
# =============================================================================
# ny.lang.diagnostics — Diagnostics Package
# =============================================================================
# Debugging, tracing, logging, and performance measurement.
# Mirrors C# System.Diagnostics.
#
# Provides:
#   - Debug/Trace: write, assert, fail (diagnostics.ny)
#   - Stopwatch: high-resolution elapsed time (diagnostics.ny)
#   - Activity/ActivitySource: distributed tracing (diagnostics.ny)
#   - Logger: structured logging with levels (logging.ny)
#   - Debugger hooks remain experimental until a debugger/runtime provider owns
#     their exact behavior.
#
# Cross-refs:
#   ny.lang.core  — Result<T,E>, text
#   ny.lang.io    — file/console output sinks for logging
# =============================================================================

package nil.std.diagnostics

import nil.std.diagnostics.diagnostics
import nil.std.diagnostics.logging
