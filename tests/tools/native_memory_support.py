"""Consumer-only allocation limits for native lifetime regressions.

Linux keeps RLIMIT_AS (all mapped/reserved virtual address space) at 96 MiB.
Windows uses a Job's per-process committed virtual-memory limit at 96 MiB;
this is deliberately not described as an RSS or address-space measurement.
The Windows launcher assigns the suspended child before its first instruction.
"""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


MEMORY_BYTES = 96 * 1024 * 1024


def verify_memory_limit(launcher: Path | None, *, cwd: Path,
                        environment: dict[str, str]) -> None:
    if sys.platform != "win32":
        return
    if launcher is None or not launcher.is_file():
        raise AssertionError("Windows lifetime tests require --memory-launcher")
    result = subprocess.run([str(launcher.resolve()), "--self-test"], cwd=cwd,
                            env=environment, stdin=subprocess.DEVNULL,
                            capture_output=True, text=True, encoding="utf-8", timeout=20)
    if (result.returncode != 0 or result.stderr or
            result.stdout != "Windows committed-memory cap and launcher controls verified\n"):
        raise AssertionError(f"Windows memory-limit negative control failed: {result}")


def run_memory_bounded(executable: Path, *, cwd: Path,
                       environment: dict[str, str], launcher: Path | None,
                       timeout: int = 45) -> subprocess.CompletedProcess:
    command = [str(executable)]
    options = {}
    if sys.platform == "linux":
        import resource

        def limit() -> None:
            resource.setrlimit(resource.RLIMIT_AS, (MEMORY_BYTES, MEMORY_BYTES))

        options["preexec_fn"] = limit
    elif sys.platform == "win32":
        if launcher is None:
            raise AssertionError("Windows memory launcher is missing")
        command = [str(launcher.resolve()), "--limit-bytes", str(MEMORY_BYTES),
                   "--timeout-ms", str(timeout * 1000), "--", *command]
        # The launcher enforces the execution deadline and bounded cleanup.
        # This outer timeout is a last-resort test-infrastructure guard.
        timeout += 10
    else:
        raise AssertionError("unsupported native memory-limit host: " + sys.platform)
    return subprocess.run(command, cwd=cwd, env=environment,
                          stdin=subprocess.DEVNULL, capture_output=True,
                          text=True, encoding="utf-8", timeout=timeout, **options)


def memory_metric() -> str:
    return ("96 MiB committed process memory (Windows Job)" if os.name == "nt"
            else "96 MiB address space (Linux RLIMIT_AS)")
