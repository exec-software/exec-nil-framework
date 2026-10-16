# Persistent scene workload

`src/scene_player.ny` builds an indexed terrain/tower mesh, copies its immutable
base coordinates, and advances 1–120 frames in one process. `src/scene/mesh.ny`
is also used by the original static mesh entrypoint, whose image remains
byte-identical to the phase-2 baseline.

The CLI accepts `[width height frames]`, defaulting to `128 96 12`. Dimensions
are restricted to 8–2048. Invalid arguments exit 2, failed mesh/packing checks
exit 3, and handled checkpoint validation or file I/O failures exit 4. The SDK and original compiler checkout
are not modified by this consumer.

## Frame pipeline

1. Build 313 vertices, 1,644 indices and 548 packed materials once. Copy x/y/z
   arrays element by element so the base arrays do not alias the moving scene.
2. Build the **recursive scene graph** (`src/scene/scene_graph.ny`) over the
   shared mesh: a root node binds the terrain (verts 0–289), and three
   tower-group nodes are children of the root, each with a tower node child
   binding that tower's 8 vertices. Because recursive struct *types* fail
   closed in the native backend, the hierarchy is stored as parallel scalar
   arrays (parent, first-child, next-sibling, per-node local transform, vertex
   range) and traversed by a recursive function `apply_world`.
3. Allocate four raw buffers once: `ptr<i32>` RGBA, `ptr<i64>` depth,
   `ptr<u8>` RGB, and `ptr<i64>` row hashes.
4. For each frame, update the tower-group local transforms (translation and
   fixed-point scale, scale = value*256 with 1.0 = 256) with deterministic
   modular offsets, then re-traverse the graph recursively from the root,
   composing each node's world transform down the tree and writing world
   positions into the mesh's animated arrays. Scale accumulates
   multiplicatively through the recursion as `(ws * node_scale) / 256`, and
   each vertex is scaled by the product of its ancestor scales before the
   accumulated translation is added. Integer division truncates toward zero,
   so negative scaled intermediates stay exact and byte-identical across
   routes. Terrain heights update with masked integer arithmetic. Reset every
   pixel and depth entry, project and rasterize triangles, pack RGB bytes, and
   check packing/index invariants.
4. Retain each frame's depth-passing fragment count and digest in scalar arrays.
   Each row uses FNV32 over packed RGBA words; a second FNV32 fold over the row
   hashes produces the frame digest. Both start at 2166136261 and update as
   `((hash ^ value) * 16777619) & 0xFFFFFFFF`.
5. Serialize the final geometry and frame history, atomically replace
   `scene-state.bin` in the working directory, read it with an exact size limit,
   decode its values, and compare them with live state. Verify CRC32 separately.
6. Emit a single final P3 image with per-frame telemetry comments and row hashes,
   then free all four raw buffers. Handled file-write/read failures also reach
   the normal buffer cleanup path. An unexpected STL binary-encoding error
   panics and does not follow this explicit cleanup path.

Only the final frame's pixels are emitted. The independent Python oracle
reconstructs every frame, checks earlier frames by digest and fragment count,
and compares the final image pixel for pixel. FNV32 digests are useful checks,
not collision-free proof that every intermediate pixel was correct.

## Checkpoint format, version 1

All words are little-endian. The total length is `3800 + 12 * frames` bytes.

| Offset | Contents |
| --- | --- |
| 0 | u32 magic `0x4353594E` (ASCII `NYSC`) |
| 4 | u32 version, currently 1 |
| 8, 12 | u32 width and height |
| 16, 20 | u32 frame count and final zero-based frame index |
| 24, 28 | u32 vertex count (313) and triangle count (548) |
| 32, 36 | u32 final frame digest and depth-passing fragment count |
| 40 | 313 triples of signed i32 x/y/z coordinates (3,756 bytes) |
| 3796 | One triple of u32 frame index/digest/fragments per frame |
| final 4 bytes | CRC-32/ISO-HDLC of all preceding bytes |

Ny uses the copied STL's checked little-endian read/write functions. Python
independently constructs the entire expected file with `struct.pack` and
`zlib.crc32`; byte equality is required in addition to a valid checksum.
Changing a vertex and recomputing the CRC still fails the oracle.

Before every measured process invocation, the harness seeds an invalid
checkpoint, with a different sentinel for each repeat. It retains each
resulting file and checks its SHA-256 against the
independent expected bytes, including across native/interpreter routes and
repeated invocations. A deliberately non-writing repeat is a negative control.

## Current limits and next steps

This workload saves and validates state; it does not yet restore a separate
process from that state. It exercises persistent buffers but does not measure
heap retention or qualify a leak-free runtime. Failure when a directory occupies
the checkpoint filename is checked on interpreter and native routes. Filesystem
permission failures, disk-full handling, interrupted writes, malformed external
assets, scene editing, camera input, and a window/event loop need separate
fixtures. The rendering remains a deterministic CPU workload.

The recursive scene graph is currently a fixed three-level tree over the
existing 313-vertex mesh. It validates recursive traversal and translation +
fixed-point scale composition over parallel scalar arrays on every route, but
does not yet include rotation transforms, a deeper or procedurally built
hierarchy, per-node material/geometry assignment, frustum culling, or camera
movement. Those are natural next increments.

The debugger audit stops inside the frame loop, enters scene updates and shared
raster functions, inspects frame/local values, then checks completed output and
checkpoint bytes against ordinary interpreter execution. The eight-route image
matrix separately establishes normal native/interpreter equivalence. DAP/JSONL
debugging is NIL-only in this copied SDK; native DWARF/attach is unavailable.
