@pragma once
# =============================================================================
# Ny Standard Library — Root Package Manifest
# =============================================================================
# Declares all first-party packages that make up the Ny standard library.
# Each package lives in its own subdirectory and has its own package.ny.
#
# Naming convention: ny.lang.* (language/runtime packages)
#                    ny.ui.*   (UI frameworks)
#                    ny.engine (game/rendering engine)
#
# Usage:
#   import ny.lang.core          # core types, traits, memory
#   import ny.lang.collections   # List<T>, HashMap<K,V>, etc.
#   import ny.lang.io            # File, Stream, Console
#   import ny.lang.math          # Math, trig, vectors
#   import ny.lang.threading     # Thread, Mutex, Channel
#   import ny.lang.text          # StringBuilder, Regex
#   import ny.lang.net           # HttpClient, Socket
#   import ny.lang.security      # Hash, Crypto, Base64
#   import ny.lang.diagnostics   # Debug, Logger, Stopwatch
#   import ny.lang.datetime      # Wall-clock time and sleep
#   import ny.lang.db            # Connection, QueryBuilder
#   import ny.lang.json          # JsonValue, to_json, from_json
#   import ny.lang.linq          # Enumerable query operators
#   import ny.lang.system        # Environment, Process
#   import ny.lang.runtime       # GC, memory stats
#   import ny.lang.concurrency   # Task<T>, async/await
#   import ny.lang.testing       # @test, assert_equals
#   import ny.ui.mimic           # Window, Button, Widget
#   import ny.engine             # ECS, Renderer, Physics
# =============================================================================

package ny

pub use nil.std.core
pub use nil.std.collections
pub use nil.std.io
pub use nil.std.math
pub use nil.std.threading
pub use nil.std.text
pub use nil.std.net
pub use nil.std.security
pub use nil.std.diagnostics
pub use nil.std.datetime
pub use nil.std.db
pub use nil.std.json
pub use nil.std.linq
pub use nil.std.system
pub use nil.std.runtime
pub use nil.std.concurrency
pub use nil.std.testing
pub use nil.std.nildata
pub use ny.ui.mimic
pub use ny.engine
