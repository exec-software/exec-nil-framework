@pragma once

# ny.lang.threading currently provides timed suspension through the
# interpreter's TIME_SLEEP opcode. Thread creation, scheduling, atomics,
# channels, and synchronization primitives remain under `experimental/` until
# they have real runtime providers and behavioral tests.
package nil.std.threading

import nil.std.threading.thread
