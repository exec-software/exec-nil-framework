#!/usr/bin/env python3
"""Minimal P3 PPM reader that accepts arbitrary whitespace between samples.

P3 separates raster samples with any whitespace, so a file may put each
sample on its own line. Reading tokens rather than lines makes this parser
accept both that shape and the conventional one-samples-per-line shape.
"""
from __future__ import annotations


class PpmError(ValueError):
    pass


def read_p3(path):
    """Return (width, height, samples) where samples is bytes of RGB triples."""
    text = path.read_text(encoding='ascii')
    tokens = []
    for line in text.splitlines():
        data, _, _ = line.partition('#')
        tokens.extend(data.split())
    if len(tokens) < 4 or tokens[0] != 'P3':
        raise PpmError(f'{path}: expected a complete P3 image')
    try:
        width, height, maximum = (int(t) for t in tokens[1:4])
    except ValueError as exc:
        raise PpmError(f'{path}: non-integer header: {exc}') from exc
    if not (8 <= width <= 2048 and 8 <= height <= 2048):
        raise PpmError(f'{path}: dimensions out of range: {width}x{height}')
    if maximum != 255:
        raise PpmError(f'{path}: unsupported channel maximum: {maximum}')
    try:
        values = bytes(int(t) for t in tokens[4:])
    except ValueError as exc:
        raise PpmError(f'{path}: non-integer raster sample: {exc}') from exc
    if len(values) != width * height * 3:
        raise PpmError(f'{path}: expected {width * height * 3} samples, got {len(values)}')
    return width, height, values


def write_png(path, width, height, samples):
    """Write an RGB8 image as a PNG, using the stdlib only."""
    import struct
    import zlib

    def chunk(kind, data):
        body = kind + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body))

    rows = b''.join(b'\x00' + samples[y * width * 3:(y + 1) * width * 3]
                    for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'
                     + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(rows, 9))
                     + chunk(b'IEND', b''))
