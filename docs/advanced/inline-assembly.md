# Native register assembly

Ny's initial inline-assembly profile is **x86_64, native-only, register-only**.
It is not a general assembler, a portable intrinsic language, or an escape
hatch for arbitrary memory accesses. The syntax and profile are provisional
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
string escapes such as `\n` are decoded once before assembly.

The template is a bounded sequence of supported AT&T-syntax integer operations:
`nop`, `movq`, `bswapq`, `addq`, `subq`, `andq`, `orq`, `xorq`, `notq`, `negq`,
and immediate `salq`/`shlq`/`shrq`/`sarq`/`rolq`/`rorq`. Only the output register
may be modified, and it must be initialized before being read. The validator
rejects explicit registers, memory operands, labels, directives, branches,
calls, returns, stack manipulation, and instructions with hidden register
outputs. Unsupported instructions are errors, not passthrough text.

LLVM receives a side-effecting, conservatively stack-aligned call, an
early-clobber output, read-only register inputs, and conservative memory/flags
clobbers. This follows the [LLVM inline-assembly operand and effect contract](https://llvm.org/docs/LangRef.html#inline-assembler-expressions).
The host target must match `x86_64`; Ny does not silently translate instructions
for another architecture.

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

`ny.inline-assembly` exercises source typechecking, textual/binary NIL,
interpreter/bundle rejection, native JIT/AOT, and split assembly/linking.
`inline-assembly.contract` checks the template validator independently.
