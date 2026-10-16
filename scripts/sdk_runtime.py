"""Canonical NIL runtime artifacts and bounded shared-library validation.

Header checks identify a loadable-library container, not a complete ABI proof.
The isolated smoke process additionally resolves and exercises the runtime ABI.
"""

from pathlib import Path
import subprocess
import sys


STATIC_NAMES = ("libnil.a", "nil.lib")
SHARED_NAMES = ("libnil.so", "libnil.dylib", "nil.dll")
# MinGW uses the GNU archive spelling; MSVC uses nil.lib for the same
# static-default/DLL pair. Neither platform permits shipping both archives.
RUNTIME_PAIRS = {"libnil.so": ("libnil.a",), "libnil.dylib": ("libnil.a",),
                 "nil.dll": ("nil.lib", "libnil.a")}


def shared_library_valid(path: Path) -> bool:
    try:
        size = path.stat().st_size
        if not 64 <= size <= 512 * 1024 * 1024:
            return False
        with path.open("rb") as stream:
            header = stream.read(4096)
        if path.name.endswith(".so"):
            if header[:4] != b"\x7fELF" or header[4] not in (1, 2) or header[5] not in (1, 2):
                return False
            endian = "little" if header[5] == 1 else "big"
            integer = lambda start, width: int.from_bytes(header[start:start + width], endian)
            if integer(16, 2) != 3 or header[6] != 1:  # ET_DYN
                return False
            is64 = header[4] == 2
            offset = integer(32 if is64 else 28, 8 if is64 else 4)
            stride = integer(54 if is64 else 42, 2)
            count = integer(56 if is64 else 44, 2)
            return count > 0 and stride >= (56 if is64 else 32) and offset >= (64 if is64 else 52) and offset + stride * count <= size
        if path.name.endswith(".dylib"):
            formats = {b"\xce\xfa\xed\xfe": ("little", 28), b"\xcf\xfa\xed\xfe": ("little", 32),
                       b"\xfe\xed\xfa\xce": ("big", 28), b"\xfe\xed\xfa\xcf": ("big", 32)}
            if header[:4] not in formats:
                return False
            endian, start = formats[header[:4]]
            return (int.from_bytes(header[12:16], endian) == 6 and  # MH_DYLIB
                    int.from_bytes(header[16:20], endian) > 0 and
                    8 <= int.from_bytes(header[20:24], endian) <= size - start)
        if path.name.endswith(".dll") and header[:2] == b"MZ":
            offset = int.from_bytes(header[60:64], "little")
            if offset < 64 or offset + 26 > size:
                return False
            with path.open("rb") as stream:
                stream.seek(offset)
                pe = stream.read(26)
            optional = int.from_bytes(pe[20:22], "little")
            sections = int.from_bytes(pe[6:8], "little")
            return (pe[:4] == b"PE\0\0" and int.from_bytes(pe[22:24], "little") & 0x2000 != 0 and
                    pe[24:26] in (b"\x0b\x01", b"\x0b\x02") and sections > 0 and
                    optional >= 96 and offset + 24 + optional + 40 * sections <= size)
    except OSError:
        pass
    return False


def shared_runtime_smoke(path: Path) -> str | None:
    # Keep loader faults or ABI failures out of the validating process.
    program = r'''
import ctypes as c, sys
lib = c.CDLL(sys.argv[1])
def bind(name, result, *arguments):
    fn = getattr(lib, name)
    fn.restype, fn.argtypes = result, list(arguments)
    return fn
abi = bind("nyrt_abi_version", c.c_uint32)
begin = bind("nyrt_context_begin", None, c.c_int, c.POINTER(c.c_char_p))
end = bind("nyrt_context_end", None)
new = bind("nyrt_array_new", c.c_void_p, c.c_uint32, c.c_int64)
put = bind("nyrt_array_set", c.c_int32, c.c_void_p, c.c_uint32, c.c_int64, c.c_uint64)
get = bind("nyrt_array_get", c.c_uint64, c.c_void_p, c.c_uint32, c.c_int64)
length = bind("nyrt_array_length", c.c_int64, c.c_void_p, c.c_uint32)
if abi() != 1: raise RuntimeError("unexpected runtime ABI version")
for iteration in range(2):
    begin(0, None)
    try:
        values = new(2, 2)  # NYRT_ARRAY_I64, unchanged ABI.
        if not values or not put(values, 2, 1, 123456789): raise RuntimeError("array allocation/write failed")
        if length(values, 2) != 2 or get(values, 2, 1) != 123456789: raise RuntimeError("array round-trip failed")
    finally:
        end()
print("NIL shared runtime ABI passed")
'''
    try:
        result = subprocess.run([sys.executable, "-c", program, str(path.resolve())],
                                capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as error:
        return "shared runtime smoke could not run: " + str(error)
    if result.returncode != 0 or result.stdout != "NIL shared runtime ABI passed\n" or result.stderr:
        return "shared runtime ABI smoke failed: exit={} {}{}".format(result.returncode, result.stdout, result.stderr).strip()
    return None
