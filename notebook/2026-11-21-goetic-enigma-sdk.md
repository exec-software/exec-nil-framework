# Goetic and Enigma development SDK integration

## Status and question

Implementation integrated; twenty focused Linux contracts pass without skips.
Full source and installed SDK qualification is in progress. This is a development
SDK candidate, not a stable release or a live application migration.

Goetic needs its qualified compiler fixes together with Enigma's durable I/O
surface, including `nil.std.io.file_lock_is_held`. The existing Goetic SDK came
from clean source `332b6f5`; the durable branch did not contain that history.
No retained qualified SDK combined both lineages.

## Source integration

An isolated ordinary merge combines `2bed655` (the compiler fixes plus current
editor corrections) with `c2fa80b` (durable implementation through `dbb4379`,
followed by the F93 findings note). Existing development branches, SDK
generations, consumer pins and the shared SDK selection are preserved.

The merge retains rooted file snapshots and WebView contexts alongside durable
file helpers and context-owned locks. The editor tree remains byte-identical
to `2bed655`. Text choice fields use the existing recursive canonical type
normalization; nominal Result schema checks and resolved function identities
remain in place.

## Review corrections

- Private-directory creation checks effective ownership and exact `0700`
  permissions before reporting success. A restrictive umask returns an error
  and can leave the new directory; the runtime does not alter the process
  umask, repair permissions through a path, or adopt existing entries. Bounded
  child tests exercise masks `0000`, `0077`, `0700` and `0777`.
- Durable intrinsic dispatch must validate the library operand before argument
  consumption or host effects. Textual and binary NIL regressions check
  foreign-library rejection and absence of filesystem mutation.

## Qualification contract

The focused Release/LLVM-ON run passes all twenty contracts, including existing
module identities, checked conversions, rooted file snapshots, ordinary I/O,
native ABI and same-process checkpoints. Console state has 58 checks, durable
VM lifecycle has 250, and the native console contract has 202 executions/checks.
The durable route report contains eight successful Ny/Matrix executions,
twelve malformed-schema rejections and 36 foreign-library rejections.
The focused JUnit SHA-256 is
`376498e076efdc305fe452ca423c08b307ce6bbfced4af63a84a5252757a919a`.

Both corrections have retained old-behavior controls. Original durable helper
tests pass against the old directory helper; adding the umask cases fails that
helper, and the corrected helper passes. Before the VM guard, a foreign-library
durable call returns success and truncates the sentinel. After the guard, every
foreign-library case rejects and preserves the sentinel and namespace. An
initial corrected run also wrote an automatic `.nycrash` diagnostic into the
oracle directory; the fixture now disables that optional report while retaining
stderr and process logs. The failed report remains part of the evidence.

The full Linux source suite includes durable helper and VM-lifecycle tests,
the filesystem-oracle route contract, optional console input and paired
Ny/Matrix choice-text-field and secure-hex fixtures. The installed fixed-output
campaign adds those four fixtures to its source/binary-library inventory.
Durable I/O and console EOF need separate side-effect and stdin-aware checks;
an ordinary stdout-only route pass cannot establish them.

Qualification must resolve one physical SDK generation for the whole run,
verify its source revision and complete payload before and after, and retain
the exact installed runtime selection and reports. Publication uses a private
alias; Goetic owns adoption at its next safe build boundary. Live data migration
and an Enigma application-record API require their own application evidence.

## Boundaries and follow-up

Durable filesystem primitives are currently POSIX-only. Windows rejection
checks prove explicit unsupported behavior, not parity. macOS needs its own
current-source execution evidence. Synchronization tests do not qualify storage
hardware against power loss. Native text remains NUL-free.

F93 remains open: a later Matrix local named `result` can interfere with an
imported `Result` owner in another paragraph. Enigma retains its naming
workaround; no loader check is relaxed. The combined compiler reproduces the
defect on textual/binary VM, AOT and JIT; renaming only that local to `gathered`
passes all four routes. Both variants pass analysis, so a successful analysis
alone is not a workaround. F21 definite assignment and native
arrays of nominal records also remain outside this integration.

Follow-up: record exact source/SDK identities and source, installed and hosted
results here after qualification, then deliver the immutable development SDK
to Goetic without changing the shared default.
