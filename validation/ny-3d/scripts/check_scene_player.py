#!/usr/bin/env python3
"""Independent multiframe geometry, raster and binary checkpoint oracle.

Expected vertices come from the documented terrain/tower formulas, and expected
bytes come from Python's struct and zlib implementations. The Ny output never
supplies reference geometry, hashes, fragment counts, or checkpoint contents.
"""
from __future__ import annotations

import argparse
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

from check_mesh_render import edge, illumination, quotient, scene
from check_render import read_ppm


def frame_vertices(base_vertices, frame):
    """Move each frame from immutable initial coordinates; do not accumulate.

    Terrain animates per-vertex. Towers move through the recursive scene graph:
    each tower k is a leaf under a transform-only group node (node k+1). The
    graph composes the group's animated translation (tx/tz) and fixed-point
    scale (256 = 1.0) with the tower's baked base coordinates:

        x' = quotient(base_x * scale_k, 256) + tx_k
        z' = quotient(base_z * scale_k, 256) + tz_k
        y' = quotient(base_y * scale_k, 256)

    with scale_k = 256 + ((frame * (13 + 6k)) % 129) - 64. This mirrors
    scene.scene_graph.apply_world exactly; integer division truncates toward
    zero, matching Ny's '/' on both interpreter and native.
    """
    vertices = []
    for i, (x, y, z) in enumerate(base_vertices):
        if i < 289:
            y += ((frame * 7 + i * 3) & 31) - 15
        else:
            tower = (i - 289) // 8
            tx = (frame * (tower + 3) + tower * 17) % 97 - 48
            tz = (frame * (tower + 5) + tower * 13) % 83 - 41
            scale = 256 + (frame * (13 + 6 * tower)) % 129 - 64
            # Truncate toward zero, matching Ny integer division on negative
            # intermediates (verified on interpreter and native).
            x = quotient(x * scale, 256) + tx
            y = quotient(y * scale, 256)
            z = quotient(z * scale, 256) + tz
        vertices.append((x, y, z))
    return vertices


def render_frame(width, height, vertices, triangles):
    projected = [(width * 8 + quotient((4 * x - 4 * z) * height, 640),
                  height * 9 + quotient((2 * x + 2 * z - 6 * y) * height, 960),
                  3 * x + 4 * y + 3 * z) for x, y, z in vertices]
    pixels = [((18 + y * 22 // height), (28 + y * 28 // height),
               (48 + y * 36 // height)) for y in range(height) for _ in range(width)]
    depth = [-1_000_000_000] * (width * height)
    fragments = 0
    for ia, ib, ic, material in triangles:
        a, b, c = (projected[i] for i in (ia, ib, ic))
        area = edge(a, b, c)
        if not area:
            continue
        if area < 0:
            b, c, area = c, b, -area
        bounds = [(max(0, quotient(min(p[axis] for p in (a, b, c)), 16)),
                   min(size - 1, quotient(max(p[axis] for p in (a, b, c)), 16)))
                  for axis, size in ((0, width), (1, height))]
        light = illumination(vertices[ia], vertices[ib], vertices[ic])
        flags = material >> 24
        base = tuple((material >> shift) & 255 for shift in (16, 8, 0))
        for y in range(bounds[1][0], bounds[1][1] + 1):
            for x in range(bounds[0][0], bounds[0][1] + 1):
                point = (16 * x + 8, 16 * y + 8)
                weights = (edge(b, c, point), edge(c, a, point), edge(a, b, point))
                if min(weights) < 0:
                    continue
                z = quotient(sum(w * p[2] for w, p in zip(weights, (a, b, c))), area)
                index = y * width + x
                if z <= depth[index]:
                    continue
                intensity = max(light, 220) if flags & 4 else light
                if flags & 2:
                    intensity += ((x ^ y) & 1) * 6
                if flags & 1 and min(weights) * 32 < area:
                    intensity = intensity * 55 // 100
                pixels[index] = tuple(channel * intensity // 255 for channel in base)
                depth[index] = z
                fragments += 1
    rgb = bytes(channel for pixel in pixels for channel in pixel)
    rows = []
    for y in range(height):
        checksum = 2166136261
        for r, g, b in pixels[y * width:(y + 1) * width]:
            packed = 0xFF000000 | (r << 16) | (g << 8) | b
            checksum = ((checksum ^ packed) * 16777619) & 0xFFFFFFFF
        rows.append(checksum)
    digest = 2166136261
    for checksum in rows:
        digest = ((digest ^ checksum) * 16777619) & 0xFFFFFFFF
    return rgb, rows, fragments, digest


@lru_cache(maxsize=4)
def reference(width, height, frames):
    base_vertices, triangles = scene()
    telemetry = []
    for frame in range(frames):
        vertices = frame_vertices(base_vertices, frame)
        rgb, rows, fragments, digest = render_frame(width, height, vertices, triangles)
        telemetry.append((frame, digest, fragments))
    # Signed vertex encoding is independent of Ny's shifts and byte-array API.
    payload = struct.pack('<10I', 0x4353594E, 1, width, height, frames, frames - 1,
                          len(vertices), len(triangles), digest, fragments)
    payload += b''.join(struct.pack('<3i', *point) for point in vertices)
    payload += b''.join(struct.pack('<3I', *record) for record in telemetry)
    crc = zlib.crc32(payload)
    return rgb, rows, tuple(telemetry), payload + struct.pack('<I', crc)


def unique_number(source, expression, label, errors, prefix):
    matches = re.findall(expression, source, re.MULTILINE)
    candidates = [line for line in source.splitlines() if line.startswith(prefix)]
    if len(matches) != 1 or len(candidates) != 1:
        errors.append(f'{label}: expected exactly one valid metadata line, found {len(matches)}')
        return None
    return int(matches[0])


def checkpoint_result(path, expected):
    result = {'path': str(path.resolve()), 'expected_bytes': len(expected),
              'expected_sha256': hashlib.sha256(expected).hexdigest(),
              'expected_crc32': struct.unpack('<I', expected[-4:])[0]}
    try:
        actual = path.read_bytes()
    except OSError as error:
        return dict(result, status='FAIL', error=str(error), matches=False)
    stored_crc = struct.unpack('<I', actual[-4:])[0] if len(actual) >= 4 else None
    calculated_crc = zlib.crc32(actual[:-4]) if len(actual) >= 4 else None
    header_fields = ('magic', 'version', 'width', 'height', 'frames', 'final_frame',
                     'vertices', 'triangles', 'final_digest', 'final_fragments')
    result.update(actual_bytes=len(actual), actual_sha256=hashlib.sha256(actual).hexdigest(),
                  actual_crc32=stored_crc, calculated_crc32=calculated_crc,
                  crc_valid=stored_crc is not None and stored_crc == calculated_crc,
                  matches=actual == expected,
                  header=(dict(zip(header_fields, struct.unpack('<10I', actual[:40])))
                          if len(actual) >= 40 else None),
                  first_differing_offset=next((i for i, (a, b) in enumerate(zip(actual, expected))
                                               if a != b),
                                              min(len(actual), len(expected))
                                              if len(actual) != len(expected) else None))
    result['status'] = 'PASS' if result['matches'] else 'FAIL'
    return result


def check(path, checkpoint=None):
    path = Path(path)
    checkpoint = Path(checkpoint) if checkpoint is not None else path.with_name('scene-state.bin')
    result = {'schema': 'ny.3d.scene-player-reference.v1', 'image': str(path.resolve()),
              'reference': 'Independent Python animated geometry, integer raster, row/frame '
                           'hashes and struct/zlib binary checkpoint',
              'status': 'FAIL', 'errors': []}
    errors = result['errors']
    try:
        width, height, actual, actual_rows = read_ppm(path)
        source = path.read_text(encoding='ascii')
    except (OSError, ValueError, UnicodeError) as error:
        errors.append(f'Invalid image: {error}')
        return result
    frames = unique_number(source, r'^# frames=(\d+)$', 'frames', errors, '# frames=')
    if frames is None or not 1 <= frames <= 120:
        if frames is not None:
            errors.append('frames must be in the bounded range 1..120')
        return result
    expected, expected_rows, telemetry, expected_state = reference(width, height, frames)
    actual_telemetry = [(int(frame), int(digest), int(fragments)) for frame, fragments, digest
                        in re.findall(r'^# frame=(\d+) fragments=(\d+) digest=(\d+)$',
                                      source, re.MULTILINE)]
    malformed_telemetry = [line for line in source.splitlines() if line.startswith('# frame=')
                           and not re.fullmatch(r'# frame=\d+ fragments=\d+ digest=\d+', line)]
    if malformed_telemetry:
        errors.append('Malformed per-frame telemetry')
    actual_fragments = unique_number(source, r'^# depth-passing fragments=(\d+)$',
                                    'final fragments', errors, '# depth-passing fragments=')
    checkpoint_metadata = re.findall(r'^# checkpoint bytes=(\d+) crc32=(\d+)$', source, re.MULTILINE)
    expected_crc = struct.unpack('<I', expected_state[-4:])[0]
    checkpoint_metadata_matches = (checkpoint_metadata == [(str(len(expected_state)), str(expected_crc))]
                                   and sum(line.startswith('# checkpoint ') for line in source.splitlines()) == 1)
    if not checkpoint_metadata_matches:
        errors.append('Checkpoint metadata differs from independently calculated length or CRC32')
    counts_match = ([line for line in source.splitlines() if line.startswith('# mesh ')]
                    == ['# mesh vertices=313 triangles=548'])
    different = [i for i in range(width * height)
                 if actual[i * 3:i * 3 + 3] != expected[i * 3:i * 3 + 3]]
    state_result = checkpoint_result(checkpoint, expected_state)
    result.update(width=width, height=height, frames=frames, pixels=width * height,
                  frame_pixels_calculated=frames * width * height,
                  final_frame_pixels_compared=width * height,
                  vertices=313, triangles=548, mesh_counts_match=counts_match,
                  actual_rgb_sha256=hashlib.sha256(actual).hexdigest(),
                  expected_rgb_sha256=hashlib.sha256(expected).hexdigest(),
                  differing_pixels=len(different),
                  max_channel_error=max((abs(a - b) for a, b in zip(actual, expected)), default=0),
                  row_hashes_match=actual_rows == expected_rows,
                  telemetry_matches=actual_telemetry == list(telemetry),
                  actual_telemetry=[dict(frame=f, digest=d, fragments=n) for f, d, n in actual_telemetry],
                  expected_telemetry=[dict(frame=f, digest=d, fragments=n) for f, d, n in telemetry],
                  actual_fragments=actual_fragments, expected_fragments=telemetry[-1][2],
                  checkpoint_metadata_matches=checkpoint_metadata_matches,
                  checkpoint=state_result,
                  artifacts={'scene-state.bin': {
                      'expected_sha256': state_result['expected_sha256'],
                      'actual_sha256': state_result.get('actual_sha256'),
                      'status': state_result['status']}},
                  mismatch_examples=[dict(x=i % width, y=i // width,
                                          actual=list(actual[i * 3:i * 3 + 3]),
                                          expected=list(expected[i * 3:i * 3 + 3])) for i in different[:8]])
    if (not errors and actual == expected and actual_rows == expected_rows and counts_match
            and actual_telemetry == list(telemetry) and actual_fragments == telemetry[-1][2]
            and state_result['status'] == 'PASS'):
        result['status'] = 'PASS'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--checkpoint', type=Path,
                        help='Defaults to scene-state.bin next to the image')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = check(args.image, args.checkpoint)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
