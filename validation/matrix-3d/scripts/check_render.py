#!/usr/bin/env python3
"""Independent Python float64 oracle for the Matrix-rendered P3 image.

This reimplements the scene from its specification -- geometry, intersection,
shading and integer conversion -- without reading any output of the Matrix
renderer. It is written to mirror the renderer's arithmetic *operation for
operation*, because that is what makes an exact comparison meaningful:

  * vectors are three separate scalars, never a tuple, so every dot product
    sums its three terms left to right;
  * normalisation divides by the square root rather than multiplying by a
    reciprocal, and recomputes that square root per component, exactly as the
    renderer does;
  * the ground plane is tested after every sphere and wins only when strictly
    closer, matching the renderer's evaluation order;
  * integer conversion truncates toward zero, which -- on the non-negative
    samples PPM allows -- is the round-half-up the renderer asks for.

Anything the renderer and this file disagree on is therefore a disagreement
about the scene, not a rounding artefact.
"""
from __future__ import annotations

import argparse
import hashlib
import math
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ppm import read_p3

SPHERE_COUNT = 5
GROUND_Y = -1.0

CENTRE_X = (0.0, -1.6, 1.6, -0.55, 0.75)
CENTRE_Y = (-0.1, -0.35, -0.35, -0.55, -0.6)
CENTRE_Z = (-3.0, -3.4, -3.4, -2.2, -2.4)
RADIUS = (0.9, 0.65, 0.65, 0.45, 0.4)
RED = (0.85, 0.3, 0.95, 0.55, 0.4)
GREEN = (0.3, 0.75, 0.8, 0.4, 0.85)
BLUE = (0.3, 0.85, 0.3, 0.9, 0.45)

FLOOR_LIT = (0.86, 0.86, 0.86)
FLOOR_DARK = (0.24, 0.3, 0.36)


def normalise(x, y, z):
    """Mirror the renderer's per-component normalisation exactly."""
    root = math.sqrt(x * x + y * y + z * z)
    return x / root, y / root, z / root


def dot3(ax, ay, az, bx, by, bz):
    return (ax * bx) + (ay * by) + (az * bz)


LIGHT = normalise(-0.4, 0.8, -0.45)


def clamp01(v):
    if v < 0.0:
        return 0.0
    if v > 1.0:
        return 1.0
    return v


def sample_of(channel):
    """`becomes number` truncates toward zero; samples are non-negative."""
    return int(clamp01(channel) * 255.0 + 0.5)


def checker(x, z):
    cell = math.floor(x) + math.floor(z)
    return cell % 2 == 0


def intersect(ox, oy, oz, dx, dy, dz):
    nearest = 1000000.0
    struck = False
    which = 0

    for index in range(SPHERE_COUNT):
        to_x = ox - CENTRE_X[index]
        to_y = oy - CENTRE_Y[index]
        to_z = oz - CENTRE_Z[index]
        radius = RADIUS[index]

        a = dot3(dx, dy, dz, dx, dy, dz)
        half_b = dot3(to_x, to_y, to_z, dx, dy, dz)
        c = dot3(to_x, to_y, to_z, to_x, to_y, to_z) - (radius * radius)
        discriminant = (half_b * half_b) - (a * c)

        if discriminant >= 0.0:
            root = math.sqrt(discriminant)
            near = (-half_b - root) / a
            if near > 0.001 and near < nearest:
                nearest = near
                struck = True
                which = index

    if dy < 0.0:
        floor_distance = (GROUND_Y - oy) / dy
        if floor_distance > 0.001 and floor_distance < nearest:
            nearest = floor_distance
            struck = True
            which = -1

    return struck, nearest, which


def shade(base, normal):
    lambert = dot3(*normal, *LIGHT)
    lit = 0.22 + (0.85 * clamp01(lambert))
    return tuple(channel * lit for channel in base)


def reference(width, height):
    """Return (samples, row_hashes) computed independently of the renderer."""
    aspect = width / height
    samples = bytearray()
    row_hashes = []

    for row in range(height):
        checksum = 2166136261
        for column in range(width):
            u = ((((column + 0.5) / width) * 2.0) - 1.0) * aspect
            v = 1.0 - (((row + 0.5) / height) * 2.0)
            dx, dy, dz = normalise(u, v, -1.0)

            struck, distance, which = intersect(0.0, 0.0, 0.0, dx, dy, dz)

            if struck:
                hit_x = dx * distance
                hit_y = dy * distance
                hit_z = dz * distance
                if which >= 0:
                    normal = normalise(hit_x - CENTRE_X[which],
                                       hit_y - CENTRE_Y[which],
                                       hit_z - CENTRE_Z[which])
                    colour = (RED[which], GREEN[which], BLUE[which])
                else:
                    normal = (0.0, 1.0, 0.0)
                    colour = FLOOR_LIT if checker(hit_x, hit_z) else FLOOR_DARK
                r, g, b = shade(colour, normal)
            else:
                t = 0.5 * (dy + 1.0)
                r = (1.0 - t) + (t * 0.35)
                g = (1.0 - t) + (t * 0.58)
                b = (1.0 - t) + (t * 0.95)

            triple = (sample_of(r), sample_of(g), sample_of(b))
            samples.extend(triple)
            packed = 0xFF000000 | (triple[0] << 16) | (triple[1] << 8) | triple[2]
            checksum = ((checksum ^ packed) * 16777619) & 0xFFFFFFFF
        row_hashes.append(checksum)

    return bytes(samples), row_hashes


def read_row_hashes(path):
    """Row hashes may be carried in trailing PPM comments; nil if absent."""
    hashes = []
    in_hashes = False
    for line in path.read_text(encoding='ascii').splitlines():
        _, _, comment = line.partition('#')
        marker = comment.strip()
        if marker == 'row-hashes fnv32 over packed rgba':
            in_hashes = True
        elif in_hashes and marker:
            hashes.append(int(marker))
    return hashes or None


def check(path):
    width, height, actual = read_p3(path)
    expected, expected_rows = reference(width, height)

    differing = [i for i in range(width * height)
                 if actual[i * 3:i * 3 + 3] != expected[i * 3:i * 3 + 3]]
    examples = [dict(x=i % width, y=i // width,
                     actual=list(actual[i * 3:i * 3 + 3]),
                     expected=list(expected[i * 3:i * 3 + 3]))
                for i in differing[:8]]

    actual_rows = read_row_hashes(path)
    return {
        'schema': 'matrix.3d.reference.v1',
        'image': str(path.resolve()),
        'reference': 'Independent Python float64 geometry, shading and integer conversion',
        'width': width, 'height': height, 'pixels': width * height,
        'actual_rgb_sha256': hashlib.sha256(actual).hexdigest(),
        'expected_rgb_sha256': hashlib.sha256(expected).hexdigest(),
        'differing_pixels': len(differing),
        'max_channel_error': max((abs(a - b) for a, b in zip(actual, expected)), default=0),
        'row_hashes': None if actual_rows is None else
                      {'present': True, 'match': actual_rows == expected_rows,
                       'expected': expected_rows},
        'mismatch_examples': examples,
        'status': 'PASS' if actual == expected else 'FAIL',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = check(args.image)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(__import__('json').dumps(result, indent=2) + '\n')
    print(__import__('json').dumps(result, indent=2))
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
