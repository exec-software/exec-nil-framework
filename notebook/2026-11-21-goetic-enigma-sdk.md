# Goetic and Enigma development SDK integration

## Status and question

The combined SDK at `6a0563438aff61b3cb751012ae00c323f836cf13` passes all
609 Linux source tests without skips and 1,248 installed fixed-output routes.
Its handoff is held: the actual Goetic/Enigma consumer exposed missing class
provenance through module globals and enum payloads. Corrections and fresh
consumer qualification are in progress. This remains development SDK work,
not a stable release or a live application migration.

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

The first full SDK attempt at `d08e1cf` retained 608 passes, one failure and
zero skips. Its `077` umask correctly exposed a test setup assumption: the
directory intended as a non-private `0755` control was actually created as
`0700`. The fixture now explicitly sets its own mode before inspection; neither
the runtime nor the runner's umask is weakened. This failed attempt published
no SDK. Initial hosted Apple Silicon builds also found a missing direct POSIX
`mkdtemp` declaration in the lifecycle fixture; it now includes `<unistd.h>`
explicitly. Those original failures remain retained, with fresh qualification
required after the corrections.

The corrected clean `6a05634` candidate then passed all 609 Linux source tests,
1,248 installed fixed-output routes, sixteen installed durable executions,
24 malformed-ABI rejections and 72 foreign-library rejections. Each installed
library form passed 202 console executions/checks. Independent replay verified
all 279 payload files and the unchanged shared SDK selection. Both hosted
source campaigns passed LLVM-ON coverage and the LLVM-OFF profile with its
explicit expected skips; all four host interpreter campaigns passed. Intel
and Apple Silicon macOS also passed the application campaign, including all
88 editor groups each with no exclusions. These results belong to `6a05634`,
before the actual-consumer corrections below, and do not qualify those changes.

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

### Actual consumer findings

The real snapshot adapter imports Matrix `record_owner` and `record_client`
classes. An explicit `Option<record_owner>.none()/some(owner)/unwrap()` path
compiled but failed loader admission; a guarded record wrapper and a direct
class global also lost the receiver's exact static view. Compilation alone
therefore did not establish consumer support. The held SDK manifest is
`5da8dc24656533137619628787358f3a6bd46198027ec9231c882c4d4981b219`.

The correction materializes exact global schemas before emitting access
metadata, preserves concrete enum identities in the loader's control-flow
analysis and checks actual enum tags and payload layouts during extraction.
Class payloads require proven static views; casts, erased globals, foreign
returns, external stubs and forged schemas cannot establish that provenance.
The runtime recursively validates nominal globals, including class fields and
initialization. Its cycle guard includes both backing-object and resolved
schema identity, so two different nominal wrappers cannot share a validation
result just because they share data.

The first actual-consumer replay after that correction found a further emitted
signature mismatch: an imported Matrix function returned an unqualified
`Result<Value,text>` while its specialized caller used the qualified concrete
enum. Code generation now resolves an unqualified generic enum signature only
when the registered declaration is unique, keeping explicit owners intact.
Function parameters, returns, class method metadata and global metadata share
that resolved identity. The loader continues to require exact provenance.

Before packaging, the provenance correction passed all 609 source tests with
zero skips; the later signature correction passed 25 focused contracts and the
expanded global contract. A private real-engine consumer then passed textual
and binary interpreter execution: actual owner/client creation and admission,
optional storage, put/get, close/reset, reopen with a new incarnation and
persisted-data checks. The engine's manifest oracle verified bytes, checksums
and modes. These runs used a diagnostic tool stage, not a qualified SDK.

The exact copied Goetic harness now passes loader admission and begins normal
execution, but encounters an `unwrap()` on an error result. That application
acceptance failure remains under investigation; no final SDK handoff follows
from the successful engine-only probe. The expanded small signature fixture
also passes the old tools, so it is a positive regression case, not proof of the
original compiler defect. The retained real-consumer before/after executions
establish that boundary.

Regression coverage includes imported Matrix classes used from Ny and Matrix,
textual/binary NIL, fresh interpreter instances, direct and record-wrapped
globals, explicit optional values, initialization failure and native rejection
without replacing prior output. Installed qualification repeats the contract
against both standard-library forms. The actual Goetic runner and a separate
real-engine optional-value consumer must also pass in private source copies
before a new immutable SDK is handed over. No live application or data change
is part of this check.

### Remaining boundaries

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
