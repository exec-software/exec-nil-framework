#!/usr/bin/env python3
"""Independent Win32 ctypes oracle for the durable-file route fixtures.

No framework executable, privacy predicate, or production helper is used here.
This is a local-NTFS runtime observation, not a power-loss/storage qualification.
"""
from __future__ import annotations

from contextlib import contextmanager
import ctypes as C
from ctypes import wintypes as W
import hashlib
import os
from pathlib import Path
import struct

if os.name != "nt":
    raise RuntimeError("the Windows durable oracle requires Win32")

K = C.WinDLL("kernel32", use_last_error=True)
A = C.WinDLL("advapi32", use_last_error=True)
N = C.WinDLL("ntdll", use_last_error=True)
VOID = C.c_void_p
PTR = C.POINTER
INVALID_HANDLE = VOID(-1).value
READ_CONTROL = 0x00020000
FILE_ALL_ACCESS = 0x001F01FF
FILE_ATTRIBUTE_DIRECTORY = 0x10
FILE_ATTRIBUTE_REPARSE_POINT = 0x400


class SecurityAttributes(C.Structure):
    _fields_ = [("length", W.DWORD), ("descriptor", VOID), ("inherit", W.BOOL)]


class SidAndAttributes(C.Structure):
    _fields_ = [("sid", VOID), ("attributes", W.DWORD)]


class FileInformation(C.Structure):
    _fields_ = [("attributes", W.DWORD), ("created", W.FILETIME),
                ("accessed", W.FILETIME), ("written", W.FILETIME),
                ("volume", W.DWORD), ("size_high", W.DWORD),
                ("size_low", W.DWORD), ("links", W.DWORD),
                ("index_high", W.DWORD), ("index_low", W.DWORD)]


class Acl(C.Structure):
    _fields_ = [("revision", W.BYTE), ("reserved", W.BYTE), ("size", W.WORD),
                ("count", W.WORD), ("reserved2", W.WORD)]


class AceHeader(C.Structure):
    _fields_ = [("kind", W.BYTE), ("flags", W.BYTE), ("size", W.WORD)]


class Overlapped(C.Structure):
    _fields_ = [("internal", C.c_size_t), ("internal_high", C.c_size_t),
                ("offset", W.DWORD), ("offset_high", W.DWORD), ("event", W.HANDLE)]


class IoStatus(C.Union):
    _fields_ = [("status", W.LONG), ("pointer", VOID)]


class IoStatusBlock(C.Structure):
    _fields_ = [("result", IoStatus), ("information", C.c_size_t)]


def api(library, name, arguments, result):
    function = getattr(library, name)
    function.argtypes, function.restype = arguments, result
    return function


close = api(K, "CloseHandle", [W.HANDLE], W.BOOL)
free = api(K, "LocalFree", [VOID], VOID)
current_process = api(K, "GetCurrentProcess", [], W.HANDLE)
current_thread = api(K, "GetCurrentThread", [], W.HANDLE)
open_process_token = api(A, "OpenProcessToken", [W.HANDLE, W.DWORD, PTR(W.HANDLE)], W.BOOL)
open_thread_token = api(A, "OpenThreadToken", [W.HANDLE, W.DWORD, W.BOOL, PTR(W.HANDLE)], W.BOOL)
token_information = api(A, "GetTokenInformation", [W.HANDLE, C.c_int, VOID, W.DWORD, PTR(W.DWORD)], W.BOOL)
sid_string = api(A, "ConvertSidToStringSidW", [VOID, PTR(VOID)], W.BOOL)
valid_sid = api(A, "IsValidSid", [VOID], W.BOOL)
equal_sid = api(A, "EqualSid", [VOID, VOID], W.BOOL)
sid_length = api(A, "GetLengthSid", [VOID], W.DWORD)
convert_descriptor = api(A, "ConvertStringSecurityDescriptorToSecurityDescriptorW",
                         [W.LPCWSTR, W.DWORD, PTR(VOID), PTR(W.DWORD)], W.BOOL)
create_directory = api(K, "CreateDirectoryW", [W.LPCWSTR, PTR(SecurityAttributes)], W.BOOL)
create_file = api(K, "CreateFileW", [W.LPCWSTR, W.DWORD, W.DWORD, VOID, W.DWORD, W.DWORD, W.HANDLE], W.HANDLE)
file_type = api(K, "GetFileType", [W.HANDLE], W.DWORD)
file_information = api(K, "GetFileInformationByHandle", [W.HANDLE, PTR(FileInformation)], W.BOOL)
security_information = api(A, "GetSecurityInfo", [W.HANDLE, C.c_int, W.DWORD, PTR(VOID),
    PTR(VOID), PTR(VOID), PTR(VOID), PTR(VOID)], W.DWORD)
descriptor_control = api(A, "GetSecurityDescriptorControl", [VOID, PTR(W.WORD), PTR(W.DWORD)], W.BOOL)
valid_acl = api(A, "IsValidAcl", [VOID], W.BOOL)
get_ace = api(A, "GetAce", [VOID, W.DWORD, PTR(VOID)], W.BOOL)
read_file = api(K, "ReadFile", [W.HANDLE, VOID, W.DWORD, PTR(W.DWORD), VOID], W.BOOL)
lock_file = api(K, "LockFileEx", [W.HANDLE, W.DWORD, W.DWORD, W.DWORD, W.DWORD, PTR(Overlapped)], W.BOOL)
unlock_file = api(K, "UnlockFileEx", [W.HANDLE, W.DWORD, W.DWORD, W.DWORD, PTR(Overlapped)], W.BOOL)
volume_information = api(K, "GetVolumeInformationByHandleW", [W.HANDLE, W.LPWSTR, W.DWORD,
    PTR(W.DWORD), PTR(W.DWORD), PTR(W.DWORD), W.LPWSTR, W.DWORD], W.BOOL)
query_information = api(N, "NtQueryInformationFile", [W.HANDLE, PTR(IoStatusBlock), VOID,
    W.DWORD, C.c_int], W.LONG)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def succeeded(result, operation):
    if not result:
        raise OSError(C.get_last_error(), "Windows durable oracle: " + operation)


@contextmanager
def handle(path: Path, *, write=False, data=False):
    access = READ_CONTROL | 0x80  # FILE_READ_ATTRIBUTES
    if data:
        access |= 0x80000000  # GENERIC_READ
    if write:
        access |= 0xC0000000  # GENERIC_READ | GENERIC_WRITE
    value = create_file(str(path), access, 7, None, 3,
                        0x02000000 | 0x00200000, None)  # BACKUP_SEMANTICS | OPEN_REPARSE_POINT
    require(value not in (None, INVALID_HANDLE), "cannot open an oracle entry without following reparses")
    try:
        yield value
    finally:
        succeeded(close(value), "close owned inspection handle")


@contextmanager
def effective_user():
    token = W.HANDLE()
    if not open_thread_token(current_thread(), 0x0008, True, C.byref(token)):
        require(C.get_last_error() == 1008, "cannot inspect effective thread identity")  # ERROR_NO_TOKEN
        succeeded(open_process_token(current_process(), 0x0008, C.byref(token)), "open process token")
    try:
        size = W.DWORD()
        require(not token_information(token, 1, None, 0, C.byref(size)) and
                C.get_last_error() == 122 and 0 < size.value <= 65536, "bound TokenUser buffer")
        storage = C.create_string_buffer(size.value)
        succeeded(token_information(token, 1, storage, size, C.byref(size)), "read TokenUser")
        sid = C.cast(storage, PTR(SidAndAttributes)).contents.sid
        require(valid_sid(sid), "effective user SID is invalid")
        yield sid  # Keep both the token and backing buffer alive through inspection.
    finally:
        succeeded(close(token), "close owned token handle")


def metadata(value):
    require(file_type(value) == 1, "oracle entry is not a disk object")
    info = FileInformation()
    succeeded(file_information(value, C.byref(info)), "read held file metadata")
    require(not info.attributes & FILE_ATTRIBUTE_REPARSE_POINT, "oracle entry is a reparse point")
    return info


def identity(info):
    return (info.volume, info.index_high, info.index_low, info.size_high, info.size_low,
            info.links, info.attributes, info.written.dwHighDateTime, info.written.dwLowDateTime)


def security(value, expected_user=None, directory=False):
    owner, acl, descriptor = VOID(), VOID(), VOID()
    status = security_information(value, 1, 0x00000001 | 0x00000004,
        C.byref(owner), None, C.byref(acl), None, C.byref(descriptor))
    require(status == 0 and descriptor.value, "cannot read held owner/DACL")
    try:
        control, revision = W.WORD(), W.DWORD()
        succeeded(descriptor_control(descriptor, C.byref(control), C.byref(revision)), "read descriptor control")
        require(owner.value and valid_sid(owner) and acl.value and valid_acl(acl), "missing/invalid owner or DACL")
        header = C.cast(acl, PTR(Acl)).contents
        require(8 <= header.size <= 65535, "DACL size is invalid")
        owner_bytes = C.string_at(owner, sid_length(owner))
        acl_bytes = C.string_at(acl, header.size)
        digest = hashlib.sha256(struct.pack("<H", control.value) + owner_bytes + acl_bytes).hexdigest()
        if expected_user is None:
            return {"sha256": digest}
        require(equal_sid(owner, expected_user), "entry owner is not the effective user")
        require(header.count == 1, "private DACL must contain exactly one ACE")
        pointer = VOID()
        succeeded(get_ace(acl, 0, C.byref(pointer)), "read private allow ACE")
        ace = C.cast(pointer, PTR(AceHeader)).contents
        require(ace.kind == 0 and 16 <= ace.size <= header.size - 8, "private ACE is not an ordinary allow ACE")
        mask = C.c_uint32.from_address(pointer.value + 4).value
        trustee = pointer.value + 8
        subauthorities = C.c_ubyte.from_address(trustee + 1).value
        require(ace.size == 16 + 4 * subauthorities and mask == FILE_ALL_ACCESS,
                "private ACE has an invalid SID length or access mask")
        require(valid_sid(trustee) and equal_sid(trustee, expected_user), "private ACE grants another SID")
        propagation = 3 if directory else 0
        explicit = bool(control.value & 0x1000) and ace.flags == propagation
        inherited = ace.flags == (propagation | 0x10)
        require(explicit or inherited, "private ACE propagation/protection differs from the owner-only profile")
        return {"sha256": digest, "profile": "protected-owner" if explicit else "inherited-owner"}
    finally:
        free(descriptor)


def inspect(value, user, directory):
    info = metadata(value)
    require(bool(info.attributes & FILE_ATTRIBUTE_DIRECTORY) == directory, "oracle entry has the wrong kind")
    require(directory or info.links == 1, "private file has multiple names")
    return info, security(value, user, directory)


def require_local_ntfs(value):
    remote, io = C.c_ubyte(1), IoStatusBlock()
    require(query_information(value, C.byref(io), C.byref(remote), 1, 51) == 0 and
            io.result.status == 0 and io.information == 1 and remote.value == 0,
            "oracle requires a positively identified local filesystem")
    name, flags = C.create_unicode_buffer(261), W.DWORD()
    succeeded(volume_information(value, None, 0, None, None, C.byref(flags), name, len(name)),
              "read held volume filesystem")
    require(name.value == "NTFS" and flags.value & 0x8 and not flags.value & 0x80000,
            "oracle requires writable local NTFS with persistent ACLs")


def create_private_directory(path: Path):
    """Exclusive effective-user ownership/DACL supplied at CreateDirectoryW."""
    with effective_user() as user:
        sid = VOID()
        succeeded(sid_string(user, C.byref(sid)), "format effective user SID")
        try:
            text = C.wstring_at(sid)
        finally:
            free(sid)
        descriptor = VOID()
        succeeded(convert_descriptor("O:" + text + "D:P(A;OICI;FA;;;" + text + ")", 1,
                                     C.byref(descriptor), None), "create owner-only descriptor")
        try:
            attributes = SecurityAttributes(C.sizeof(SecurityAttributes), descriptor, False)
            succeeded(create_directory(str(path), C.byref(attributes)), "create exclusive private directory")
        finally:
            free(descriptor)
        with handle(path) as held:
            inspect(held, user, True)
            require_local_ntfs(held)


def security_hash(path: Path) -> str:
    """Read-only owner/DACL seal for a before/after nonmutation oracle."""
    with handle(path) as held:
        metadata(held)
        return security(held)["sha256"]


def host_oracle(directory: Path, expected: dict[str, bytes]) -> dict:
    expected_directories = {"private"}
    observed = {entry.name for entry in directory.iterdir()}
    require(observed == set(expected) | expected_directories, "durable namespace differs from exact fixture inventory")
    profiles, seals = {}, {}
    with effective_user() as user:
        with handle(directory) as held:
            _, result = inspect(held, user, True)
            require_local_ntfs(held)
            profiles["."] = result["profile"]
            seals["."] = result["sha256"]
        for name in sorted(expected_directories):
            with handle(directory / name) as held:
                _, result = inspect(held, user, True)
                profiles[name], seals[name] = result["profile"], result["sha256"]
                require(not any((directory / name).iterdir()), "private fixture directory must remain empty")
        for name, content in expected.items():
            with handle(directory / name, data=True) as held:
                before, result = inspect(held, user, False)
                require(before.size_high == 0 and before.size_low == len(content) <= 4096,
                        "held durable file has the wrong size")
                data, count = C.create_string_buffer(len(content) + 1), W.DWORD()
                succeeded(read_file(held, data, len(content) + 1, C.byref(count), None), "read held durable bytes")
                require(count.value == len(content) and data.raw[:count.value] == content, "durable bytes differ")
                after, after_security = inspect(held, user, False)
                require(identity(after) == identity(before) and result == after_security,
                        "held durable entry changed during its independent inspection")
                profiles[name], seals[name] = result["profile"], result["sha256"]
        with handle(directory / "writer.lock", write=True) as held:
            inspect(held, user, False)
            overlap = Overlapped()
            succeeded(lock_file(held, 0x3, 0, 1, 0, C.byref(overlap)), "acquire external nonblocking byte-zero lock")
            try:
                inspect(held, user, False)
            finally:
                succeeded(unlock_file(held, 0, 1, 0, C.byref(overlap)), "release external byte-zero lock")
    require({entry.name for entry in directory.iterdir()} == observed, "oracle namespace changed during inspection")
    return {"bytes": "exact", "filesystem": "local-NTFS", "remaining_staging_files": 0,
            "lock_after_process_completion": "released", "lock_primitive": "LockFileEx-byte-0-length-1",
            "private_directories": sorted(expected_directories), "windows_owner": "effective-user",
            "windows_dacl": "one-FILE_ALL_ACCESS-owner-allow-ACE",
            "windows_acl_profiles": profiles, "windows_acl_sha256": seals}
