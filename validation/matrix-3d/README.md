# Matrix 3D Validation

A single CPU raytracer written in Matrix, checked against an independently
written Python oracle and run across every execution route the SDK offers.

This is deliberately small: one program, one scene, one oracle. Its job is to
answer a narrow question with evidence rather than to be a renderer. That
question is whether a non-trivial Matrix program -- nested loops, floating
arithmetic, struct-of-arrays indexing, records returned from paragraphs,
standard-library transcendental functions -- means the same thing on the
interpreter and on the LLVM backends, and whether it means what an independent
reference says it means.

![Five lambert-shaded spheres on a checkered ground plane under a sky gradient, rendered in Matrix](images/matrix-spheres.png)

## The scene

Five spheres in a rough arc over an infinite checkered ground plane, lit by
one directional light with an ambient floor, against a vertical sky gradient.
Fixed at 512x384 so the image is comparable across runs and routes.

The scene lives in struct-of-arrays form -- parallel `array of floating`
columns for centres, radii and colours rather than an array of records -- so
the intersection loop indexes five arrays in lockstep and never allocates a
record per sphere.

## Validate

Build the SDK at the repository root first, then:

```bash
cd validation/matrix-3d

# Every route: the four interpreted routes, LLVM JIT, native AOT, and both
# assembler/linker pipelines.
python3 scripts/validate.py

# Interpreter and native AOT only.
python3 scripts/validate.py --quick

# A single route.
python3 scripts/validate.py --only jit
```

The harness refuses to run an SDK whose payload does not match its sealed
manifest, and verifies that manifest again afterwards, so a result is never
evidence about an SDK that was mutated mid-run. It writes `reports/validation.json`
and a `.txt` companion; `work/<route>/` keeps each route's PPM, PNG, command
records and stderr for inspection.

## What a pass means

Every route is checked twice, and a row is `PASS` only if both hold:

1. **Against an independent oracle.** `scripts/check_render.py` reimplements
   the scene from its specification in Python float64 without reading anything
   the renderer produced. It mirrors the renderer's arithmetic operation for
   operation -- three-scalar dot products summed left to right, division by a
   square root rather than multiplication by a reciprocal, the ground plane
   tested after every sphere and winning only when strictly closer, and
   truncation-toward-zero integer conversion. A pass therefore means the two
   agree on every one of the 196,608 pixels, not merely that they agree with
   each other.
2. **Against every other route.** All routes must produce byte-identical PPM.
   An image that is right on the interpreter and different on the LLVM backend
   fails here even if it would pass the oracle on its own.

**Current result: 8 of 8 routes pass, byte-identical, with zero differing
pixels against the oracle.** See `reports/validation.json`.

## Known limitation: number-to-text casts on native routes

This program writes its PPM one channel per line rather than as `r g b` triples.
That is not a style choice. P3 separates raster samples with arbitrary
whitespace and a newline is whitespace, so the output is a valid PPM, but the
shape exists because of a compiler defect this project surfaced:

```text
<number or floating> becomes text
```

compiles and runs correctly on all four interpreted routes, and fails to
compile on every native route:

```text
[NativeCodegen] ordinary CAST cannot convert 'i32' to native handle ty
```

The failure reproduces with just `say (5 becomes text)`. Text literals, text
bindings, `text plus text` and `say <number>` all work on every route; only the
numeric-to-text cast is affected. Writing the conventional `r g b` line would
need exactly that cast, so until it is fixed the renderer emits three samples
per line, which costs nothing and keeps all eight routes green.

This is a real finding rather than an expected-failure exemption: the harness
does not skip the native routes, it runs them and they pass.

## Layout

```text
src/renderer.matrix     The renderer: scene, intersection, shading, PPM output
scripts/validate.py     Route harness, report generation
scripts/check_render.py Independent float64 oracle
scripts/ppm.py          P3 reader (whitespace-agnostic) and a stdlib-only PNG writer
scripts/sdk_integrity.py Sealed-manifest verification, run before and after
images/                 Rendered previews used by the repository README
reports/                Measured results (generated, not committed)
work/                   Per-route build artifacts and outputs (generated)
```

## Scope

This matrix measures one program. It is not a language conformance suite, a
performance benchmark, or a claim about Matrix generally; it is a consumer of
the SDK written in the language itself, checking that its own output is what
an independent reference says it should be. For a much larger matrix across
two languages and eight routes, see [`../ny-3d`](../ny-3d).

Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
