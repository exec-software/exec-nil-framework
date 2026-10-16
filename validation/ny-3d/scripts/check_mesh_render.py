#!/usr/bin/env python3
"""Independent integer geometry/raster oracle for the Ny indexed mesh image.

The scene specification uses world-space tuples and triangle records here, not
Ny's separate arrays or raw buffers. Python integers avoid accidental fixed-width
overflow; division explicitly truncates toward zero to match Ny's integer math.
No Ny output, generated NIL, or compiler implementation supplies expected data.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re

from check_render import read_ppm


def quotient(numerator, denominator):
    magnitude = abs(numerator) // abs(denominator)
    return -magnitude if (numerator < 0) != (denominator < 0) else magnitude


def scene():
    vertices = []
    triangles = []
    for j in range(17):
        for i in range(17):
            noise = ((17 * i) ^ (29 * j) ^ (2 * i * j)) & 63
            height = 30 + (16 - abs(i - 8) - abs(j - 8)) * 12 + noise * 2
            vertices.append(((i - 8) * 96, height, (j - 8) * 96))
    for j in range(16):
        for i in range(16):
            a = j * 17 + i
            color = 0x71964B if (i ^ j) & 1 else 0x58845E
            triangles.extend(((a, a + 18, a + 1, 0x03000000 | color),
                              (a, a + 17, a + 18, 0x03000000 | color)))
    for cx, cz, radius, top, color in (
        (-336, -192, 120, 430, 0xE65D52),
        (288, 96, 144, 560, 0x36C6CF),
        (-144, 384, 96, 360, 0xF2B84C),
    ):
        start = len(vertices)
        footprint = ((cx - radius, cz - radius), (cx + radius, cz - radius),
                     (cx + radius, cz + radius), (cx - radius, cz + radius))
        vertices.extend((x, y, z) for y in (0, top) for x, z in footprint)
        for a, b, c, d, flags in ((0, 1, 2, 3, 1), (4, 7, 6, 5, 5),
                                  (0, 4, 5, 1, 1), (1, 5, 6, 2, 1),
                                  (2, 6, 7, 3, 1), (3, 7, 4, 0, 1)):
            material = (flags << 24) | color
            triangles.extend(((start + a, start + b, start + c, material),
                              (start + a, start + c, start + d, material)))
    assert len(vertices) == 313 and len(triangles) == 548
    return vertices, triangles


def edge(a, b, point):
    return (b[0] - a[0]) * (point[1] - a[1]) - (b[1] - a[1]) * (point[0] - a[0])


def illumination(a, b, c):
    u = tuple(y - x for x, y in zip(a, b))
    v = tuple(y - x for x, y in zip(a, c))
    nx = u[1] * v[2] - u[2] * v[1]
    ny = u[2] * v[0] - u[0] * v[2]
    nz = u[0] * v[1] - u[1] * v[0]
    norm = abs(nx) + abs(ny) + abs(nz)
    return 95 + max(0, -3 * nx + 7 * ny + 5 * nz) * 140 // (7 * norm) if norm else 95


def reference(width, height):
    vertices, triangles = scene()
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
            checksum = ((checksum ^ (0xFF000000 | (r << 16) | (g << 8) | b)) * 16777619) & 0xFFFFFFFF
        rows.append(checksum)
    return rgb, rows, fragments


def check(path):
    width, height, actual, actual_rows = read_ppm(path)
    expected, expected_rows, expected_fragments = reference(width, height)
    source = path.read_text(encoding='ascii')
    fragments = re.findall(r'^# depth-passing fragments=(\d+)$', source, re.MULTILINE)
    actual_fragments = int(fragments[0]) if len(fragments) == 1 else None
    counts_match = source.count('# mesh vertices=313 triangles=548\n') == 1
    mismatch = [i for i in range(width * height) if actual[i * 3:i * 3 + 3] != expected[i * 3:i * 3 + 3]]
    return {
        'schema': 'ny.3d.mesh-reference.v1', 'image': str(path.resolve()),
        'reference': 'Independent Python integer geometry, triangle coverage, depth, shading, packing and row hashes',
        'width': width, 'height': height, 'pixels': width * height,
        'vertices': 313, 'triangles': 548, 'mesh_counts_match': counts_match,
        'actual_fragments': actual_fragments, 'expected_fragments': expected_fragments,
        'actual_rgb_sha256': hashlib.sha256(actual).hexdigest(),
        'expected_rgb_sha256': hashlib.sha256(expected).hexdigest(),
        'differing_pixels': len(mismatch),
        'max_channel_error': max((abs(a - b) for a, b in zip(actual, expected)), default=0),
        'row_hashes_match': actual_rows == expected_rows,
        'mismatch_examples': [{'x': i % width, 'y': i // width,
                               'actual': list(actual[i * 3:i * 3 + 3]),
                               'expected': list(expected[i * 3:i * 3 + 3])} for i in mismatch[:8]],
        'status': 'PASS' if actual == expected and actual_rows == expected_rows and counts_match
                  and actual_fragments == expected_fragments else 'FAIL',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = check(args.image)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
