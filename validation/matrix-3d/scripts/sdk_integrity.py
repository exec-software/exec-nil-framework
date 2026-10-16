#!/usr/bin/env python3
"""Verify an SDK payload against its sealed manifest.

An in-tree build has no external origin anchor, so the payload is verified
against its own sealed manifest and the result is marked self-anchored. The
harness calls this before and after every run and fails if either the manifest
or any payload file changed underneath it, which is what stops a validation
result from being evidence about an SDK that was mutated mid-run.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def verify(sdk: Path) -> dict:
    result = {'status': 'FAIL', 'sdk': str(sdk), 'errors': [], 'checked_files': 0,
              'anchor': 'self (no origin anchor)'}
    errors = result['errors']
    try:
        if sdk.is_symlink():
            raise ValueError('SDK root must be an independent directory, not a symlink')
        manifest_path = sdk / 'manifest.json'
        if not manifest_path.is_file():
            raise ValueError('SDK manifest is missing')
        manifest = json.loads(manifest_path.read_text())
        result['manifest_sha256'] = digest(manifest_path)
        result['compiler_version'] = manifest.get('qualification', {}).get('compiler_version')

        for entry in manifest['payload']['files']:
            relative, expected = entry['path'], entry['sha256']
            path = sdk / relative
            if path.is_symlink() or not path.is_file():
                errors.append(f'payload file missing or a symlink: {relative}')
                continue
            if digest(path) != expected:
                errors.append(f'payload file differs from the manifest: {relative}')
            result['checked_files'] += 1
        result['status'] = 'FAIL' if errors else 'PASS'
    except (OSError, ValueError, KeyError, TypeError) as exc:
        errors.append(f'{type(exc).__name__}: {exc}')
    return result
