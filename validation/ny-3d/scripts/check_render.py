#!/usr/bin/env python3
"""Independent Python geometry/pixel oracle for the Ny-rendered P3 image."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path


SCENE = (
    ((-1.15, -0.15, 0.9), 0.85, 0xE84848, True),
    ((0.95, -0.35, 0.1), 0.65, 0x24BBC4, True),
    ((0.35, -0.45, 2.4), 0.55, 0xF5B84D, False),
    ((-0.2, 1.1, 2.3), 0.4, 0x9C77E0, True),
)
ORIGIN = (0.0, 1.1, -6.0)


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def scale(a, k):
    return tuple(x * k for x in a)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def unit(a):
    return scale(a, 1.0 / math.sqrt(dot(a, a)))


LIGHT = unit((-0.6, 0.9, -0.5))


def nearest(origin, direction):
    best = (100000.0, (0.0, 0.0, 0.0), 0, False)
    for center, radius, color, reflective in SCENE:
        offset = add(origin, scale(center, -1.0))
        b = dot(offset, direction)
        c = dot(offset, offset) - radius * radius
        discriminant = b * b - c
        if discriminant >= 0.0:
            distance = -b - math.sqrt(discriminant)
            if 0.001 < distance < best[0]:
                normal = scale(add(offset, scale(direction, distance)), 1.0 / radius)
                best = distance, normal, color, reflective
    if direction[1] < -0.00001:
        distance = (-1.0 - origin[1]) / direction[1]
        if 0.001 < distance < best[0]:
            point = add(origin, scale(direction, distance))
            checker = (math.floor(point[0]) ^ math.floor(point[2])) & 1
            best = distance, (0.0, 1.0, 0.0), (0x304657 if checker else 0xC5D0D8), False
    return best


def surface(origin, direction, hit):
    distance, normal, packed, _ = hit
    if distance >= 100000.0:
        t = min(1.0, max(0.0, direction[1] * 0.7 + 0.5))
        return 26.0 + 52.0 * t, 41.0 + 69.0 * t, 69.0 + 86.0 * t
    point = add(origin, scale(direction, distance))
    shadow = nearest(add(point, scale(normal, 0.004)), LIGHT)
    visible = 0.18 if shadow[0] < 100000.0 else 1.0
    diffuse = max(0.0, dot(normal, LIGHT))
    halfway = unit(add(LIGHT, scale(direction, -1.0)))
    specular = max(0.0, dot(normal, halfway))
    s2 = specular * specular
    s4 = s2 * s2
    s8 = s4 * s4
    s16 = s8 * s8
    shine = s16 * s16 * 110.0 * visible
    base = ((packed >> 16) & 255, (packed >> 8) & 255, packed & 255)
    return add(scale(base, 0.18 + 0.76 * diffuse * visible), (shine, shine, shine))


def pixel(direction):
    hit = nearest(ORIGIN, direction)
    color = surface(ORIGIN, direction, hit)
    distance, normal, _, reflective = hit
    if distance < 100000.0:
        if reflective:
            point = add(ORIGIN, scale(direction, distance))
            reflected = add(direction, scale(normal, -2.0 * dot(direction, normal)))
            bounce_origin = add(point, scale(normal, 0.004))
            bounce = surface(bounce_origin, reflected, nearest(bounce_origin, reflected))
            color = add(scale(color, 0.76), scale(bounce, 0.24))
        fog = min(distance * 0.018, 0.62)
        color = add(scale(color, 1.0 - fog), scale((73.0, 94.0, 124.0), fog))
    return tuple(int(min(255.0, max(0.0, channel))) for channel in color)


def reference(width, height):
    pixels = bytearray()
    row_hashes = []
    for y in range(height):
        checksum = 2166136261
        for x in range(width):
            sx = ((x + 0.5) / width * 2.0 - 1.0) * width / height
            sy = 1.0 - (y + 0.5) / height * 2.0
            r, g, b = pixel(unit((sx, sy - 0.10, 1.5)))
            pixels.extend((r, g, b))
            packed = 0xFF000000 | (r << 16) | (g << 8) | b
            checksum = ((checksum ^ packed) * 16777619) & 0xFFFFFFFF
        row_hashes.append(checksum)
    return bytes(pixels), row_hashes


def read_ppm(path):
    tokens = []
    row_hashes = []
    in_hashes = False
    for line in path.read_text(encoding="ascii").splitlines():
        data, _, comment = line.partition("#")
        tokens.extend(data.split())
        if comment.strip() == "row-hashes fnv32 over packed RGBA":
            in_hashes = True
        elif in_hashes and comment.strip():
            row_hashes.append(int(comment.strip()))
    if len(tokens) < 4 or tokens[0] != "P3":
        raise ValueError("expected a complete P3 image")
    width, height, maximum = map(int, tokens[1:4])
    if not (8 <= width <= 2048 and 8 <= height <= 2048 and maximum == 255):
        raise ValueError("invalid dimensions or channel maximum")
    values = [int(token) for token in tokens[4:]]
    if len(values) != width * height * 3:
        raise ValueError("pixel count does not match the PPM header")
    return width, height, bytes(values), row_hashes


def check(path):
    width, height, actual, actual_rows = read_ppm(path)
    expected, expected_rows = reference(width, height)
    differences = [abs(a - b) for a, b in zip(actual, expected)]
    differing_pixels = sum(any(differences[i:i + 3]) for i in range(0, len(differences), 3))
    examples = []
    for offset in range(0, len(actual), 3):
        if actual[offset:offset + 3] != expected[offset:offset + 3]:
            index = offset // 3
            examples.append({"x": index % width, "y": index // width,
                             "actual": list(actual[offset:offset + 3]),
                             "expected": list(expected[offset:offset + 3])})
            if len(examples) == 8:
                break
    return {
        "schema": "ny.3d.reference.v1", "image": str(path.resolve()),
        "reference": "Independent Python float64 geometry, shading, integer packing and row hashes",
        "width": width, "height": height, "pixels": width * height,
        "actual_rgb_sha256": hashlib.sha256(actual).hexdigest(),
        "expected_rgb_sha256": hashlib.sha256(expected).hexdigest(),
        "differing_pixels": differing_pixels, "max_channel_error": max(differences, default=0),
        "row_hashes_match": actual_rows == expected_rows,
        "mismatch_examples": examples,
        "status": "PASS" if actual == expected and actual_rows == expected_rows else "FAIL",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    result = check(args.image)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
