#!/usr/bin/env python3
"""Verify an SDK payload against its sealed manifest and, when available, its origin anchor."""
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


def verify(sdk: Path, origin_path: Path) -> dict:
    """Verify an SDK payload against its sealed manifest.

    When origin_path exists it also anchors the manifest hash to an external
    record, which is what makes a copied SDK distribution trustworthy. An
    in-tree build has no such anchor; the payload is then verified against its
    own sealed manifest and the result is marked self-anchored.
    """
    result = {'status': 'FAIL', 'sdk': str(sdk), 'errors': [], 'checked_files': 0}
    errors = result['errors']
    try:
        if sdk.is_symlink():
            raise ValueError('SDK root must be an independent directory, not a symlink')
        origin = json.loads(origin_path.read_text()) if origin_path.is_file() else None
        manifest_path = sdk / 'manifest.json'
        if manifest_path.is_symlink() or not manifest_path.is_file() or manifest_path.stat().st_nlink != 1:
            raise ValueError('SDK manifest must be an independent regular file')
        result['manifest_sha256'] = digest(manifest_path)
        result['anchor'] = 'origin file' if origin is not None else 'self (no origin anchor)'
        if origin is not None:
            result['origin_manifest_sha256'] = origin['source_manifest_sha256']
            if result['manifest_sha256'] != origin['source_manifest_sha256']:
                errors.append('Copied manifest differs from the immutable origin SHA-256 anchor')
        manifest = json.loads(manifest_path.read_text())
        result['baseline'] = {'source': manifest.get('source'), 'build': manifest.get('build'),
                              'host': manifest.get('host'),
                              'qualification_mode': manifest.get('qualification', {}).get('mode'),
                              'compiler_version': manifest.get('qualification', {}).get('compiler_version')}
        files = manifest['payload']['files']
        expected_paths = {'manifest.json'}
        if manifest['payload']['file_count'] != len(files):
            errors.append('Manifest file_count does not match payload entries')
        for item in files:
            rel = Path(item['path'])
            if rel.is_absolute() or '..' in rel.parts or item['path'] in expected_paths:
                errors.append(f'Invalid or duplicate payload path: {item["path"]}')
                continue
            expected_paths.add(item['path'])
            path = sdk / rel
            if path.is_symlink() or not path.is_file():
                errors.append(f'Missing or symlinked payload file: {rel}')
                continue
            if path.stat().st_nlink != 1:
                errors.append(f'Payload file has multiple hard links: {rel}')
            if path.stat().st_size != item['bytes'] or digest(path) != item['sha256']:
                errors.append(f'Payload size/hash mismatch: {rel}')
            result['checked_files'] += 1
        actual_paths = set()
        for path in sdk.rglob('*'):
            if path.is_symlink():
                errors.append(f'SDK contains symlink: {path.relative_to(sdk)}')
            if path.is_file():
                actual_paths.add(path.relative_to(sdk).as_posix())
        if actual_paths != expected_paths:
            errors.append(f'SDK file inventory differs: extra={sorted(actual_paths-expected_paths)}, missing={sorted(expected_paths-actual_paths)}')
        result['status'] = 'PASS' if not errors else 'FAIL'
    except (OSError, ValueError, KeyError, TypeError) as exc:
        errors.append(str(exc))
    return result


if __name__ == '__main__':
    import os
    root = Path(__file__).resolve().parents[1]
    sdk = Path(os.environ.get('NIL_SDK') or root.parents[1] / 'sdk').resolve()
    result = verify(sdk, root / 'sdk-origin.json')
    print(json.dumps(result, indent=2))
    raise SystemExit(result['status'] != 'PASS')
