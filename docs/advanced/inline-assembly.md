# Native register assembly

Ny provides bounded **x86_64 and AArch64, native-only, register-only**
inline-assembly profiles. Each template declares its architecture explicitly.
The syntax and profiles are provisional
until the release-candidate language contract is frozen.

```ny
fn swap_bytes(value: i64) -> i64
    let mut result: i64 = 0
    asm "x86_64", "movq $1, $0; bswapq $0" (value) -> result
    end asm
    return result
end fn
```

The input expressions are evaluated once, left to right. Each must have type
`i64`; use an explicit cast for smaller integers. The optional output names an
existing mutable, explicitly typed `i64` local. It is written after the block.
There are at most eight inputs and one output. With an output, `$0` denotes the
output register and `$1` denotes the first input; without one, `$0` denotes the
first input. `$$` introduces an assembler immediate (for example `$$8`). Ny
string escapes such as `\n` are decoded once before assembly. The immediate
syntax above applies to x86_64; AArch64 uses `#` for its permitted shift counts.

## x86_64 profile

The target is `"x86_64"`. Templates use supported AT&T-syntax integer operations:
`nop`, `movq`, `bswapq`, `addq`, `subq`, `andq`, `orq`, `xorq`, `notq`, `negq`,
and immediate `salq`/`shlq`/`shrq`/`sarq`/`rolq`/`rorq`. Instructions place the
destination last. `movq` accepts signed i64 decimal immediates; arithmetic and
bitwise immediates must fit signed i32. Shift and rotate counts are `$$0` through
`$$63`. `xorq $0,$0` may initialize the output to zero without reading its prior
contents.

## AArch64 profile

The target is `"aarch64"`, including on hosts that call the architecture
`arm64`. Templates use 64-bit general-purpose registers and the following
spellings only:

| Instruction | Operands |
| --- | --- |
| `nop` | None |
| `mov`, `rev`, `mvn`, `neg` | `$0, source` |
| `add`, `sub`, `and`, `orr`, `eor` | `$0, left, right` |
| `lsl`, `lsr`, `asr`, `ror` | `$0, source, #count` |

Every source, left and right operand is a register placeholder. Shift and
rotate counts are canonical decimal `#0` through `#63`; signs, leading zeroes,
expressions and register counts are rejected. Other constants must be passed
as normal i64 input expressions. This avoids assembler-dependent expansion of
immediate moves and logical operations. `rev` reverses all eight bytes; `mvn`
complements the bits; `asr` shifts in the sign bit, while `lsr` shifts in zeroes.
The listed integer arithmetic wraps at 64 bits. Flags-setting forms such as
`adds`, `subs` and `ands` are outside this profile.

```ny
fn swap_bytes(value: i64) -> i64
    let mut result: i64 = 0
    asm "aarch64", "rev $0, $1" (value) -> result
    end asm
    return result
end fn
```

Matrix uses the same target and operand rules:

```matrix
swap-bytes receives word as number returns number begin paragraph
    variable result as number is 0
    assembly for aarch64 is "rev $0, $1" using word giving result
    give result
end paragraph
```

## Operand and effect boundaries

Only the output register may be modified, and it must be initialized before
being read. A local's old value is not implicitly available in `$0`: pass it as
an input if needed. For AArch64, even `eor $0,$0,$0` is rejected before
initialization; `eor $0,$1,$1` can initialize zero from an input. The validator
rejects explicit registers, memory operands, labels, directives, branches,
calls, returns, stack manipulation, and instructions with hidden register
outputs. This includes AArch64 `sp`, `xzr`, numbered registers and register-width
modifiers. Unsupported instructions are errors, not passthrough text. Both the
source compiler and native NIL loader validate the contract.

LLVM receives a side-effecting, conservatively stack-aligned call, an
early-clobber output, read-only register inputs, and conservative target-specific
memory/condition-code clobbers. This follows the [LLVM inline-assembly operand
and effect contract](https://llvm.org/docs/LangRef.html#inline-assembler-expressions).
LLVM's [AArch64 register constraint implementation](https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/Target/AArch64/AArch64ISelLowering.cpp)
selects 64-bit general-purpose registers for i64 operands. The native target
must match the declared architecture; Ny does not silently translate
instructions for another architecture.

## Execution routes

- LLVM JIT and AOT execute the register assembly.
- Textual and binary executable NIL retain an explicit `INLINE_ASM` opcode;
  `nilassembler` plus `nillinker` executes the same profile.
- `nilinterpreter`, interpreter bundles, and the NIL debugger reject execution
  of that opcode with a native-route diagnostic. They never erase or emulate it.
- LLVM-disabled builds can produce the NIL artifact but cannot execute its
  assembly through a native route.

The historical `asm "..." end asm` form is rejected because it does not declare
a target or operands. SIMD, arbitrary register classes, explicit clobbers,
pointer operands, multiple outputs, other targets, and portable fallback
selection need separate contracts and tests; they are not implemented by this
initial profile.

`ny.inline-assembly` exercises both Ny and Matrix through textual/binary NIL,
interpreter/bundle rejection, native JIT/AOT, and split assembly/linking. It
also checks malformed input, foreign-target rejection and preservation of an
existing output after failure. `inline-assembly.contract` checks both template
validators independently on every host. LLVM-enabled acceptance requires the
matching host's actual native routes; cross-target object emission alone is
not execution evidence. See the [platform support matrix](../platform-support.md)
for the latest completed native acceptance, separately from this source contract.
