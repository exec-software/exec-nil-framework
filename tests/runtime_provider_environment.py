"""Explicit test-only Darwin provider paths, separated from build processes."""
from __future__ import annotations

from pathlib import PurePosixPath
import sys

DECLARATION = 'NIL_TEST_DARWIN_RUNTIME_LIBRARY_PATH'
LOADER_VARIABLE = 'DYLD_FALLBACK_LIBRARY_PATH'


def provider_environments(environment, *, platform_name=None):
    """Return build/runtime environments and the optional declared search path.

    Without the CI/test declaration, ambient loader settings retain their
    existing meaning. With it, a conflicting loader override is an error, not
    a setting silently removed to make a cache or qualification test pass.
    """
    build = dict(environment)
    declared = build.pop(DECLARATION, None)
    if declared is None:
        return build, dict(build), None
    if (platform_name or sys.platform) != 'darwin':
        raise ValueError('the declared runtime library search path requires macOS')
    if (not isinstance(declared, str) or not declared or len(declared) > 32768 or
            any(char in declared for char in '\0\r\n') or
            any(not part or not PurePosixPath(part).is_absolute() for part in declared.split(':'))):
        raise ValueError('runtime library path must contain ordered absolute nonempty Darwin directories')
    if any(key.startswith('DYLD_') and value for key, value in build.items()):
        raise ValueError('declared runtime library path conflicts with ambient DYLD overrides')
    runtime = dict(build, **{LOADER_VARIABLE: declared})
    return build, runtime, declared
