@schema
@pragma once
module nil.std.io

import nil.std.core
import nil.std.io.stream


# =============================================================================
# File Operations
# =============================================================================

# Check if a file exists
@intrinsic("file.exists")
pub fn exists(path: text) -> bool
end fn

# Read entire file contents as text
@intrinsic("file.read_text")
pub fn read_text(path: text) -> Result<text, text>

# Read entire file contents as bytes
@intrinsic("file.read_bytes")
pub fn read_bytes(path: text) -> Result<array<u8>, text>

# Read one bounded, regular file without following its final path component.
# The runtime checks the caller-provided limit before allocating, rejects
# special files and observed input changes, and does not include the path in
# errors. The hard runtime ceiling is 512 MiB. On POSIX this opens once with
# required no-follow, nonblocking, and close-on-exec flags, then performs its
# checks and read through that descriptor. Windows rejects reparse points but
# cannot provide the same parent-directory descriptor guarantee. Parent-
# directory symlinks are outside this API's containment contract everywhere.
@intrinsic("file.read_regular_bytes")
pub fn read_regular_bytes(path: text, maximum_bytes: i64) -> Result<array<u8>, text>

# Write text to a file (creates or overwrites)
@intrinsic("file.write_text")
pub fn write_text(path: text, content: text) -> Result<bool, text>

# Write bytes to a file (creates or overwrites)
@intrinsic("file.write_bytes")
pub fn write_bytes(path: text, content: array<u8>) -> Result<bool, text>

# Publish bytes from a same-directory staging file, so readers never observe a
# partial destination. With replace=false publication is an atomic no-clobber
# operation and rejects every existing entry, including dangling symlinks. With
# replace=true an existing destination observed during preflight must be a
# regular file; publication never follows the destination entry. A concurrent
# namespace change after preflight can replace what was inspected, so callers
# needing directory containment must control the parent directory. Errors never
# include path contents. POSIX uses descriptor-relative staging/publication and
# publishes a fresh private 0600 inode, including on replacement; prior mode,
# owner, ACLs, and extended attributes are not preserved. Windows uses
# CREATE_NEW plus MoveFileEx, the account's default DACL, and narrow host path
# APIs whose UTF-8 portability is unverified. Its namespace guarantees are also
# weaker. Staging cleanup is best effort: a rare host cleanup failure can leave
# a `.ny-write-*` orphan. Successful publication is visibility-atomic, not a
# promise of parent-directory durability across sudden power loss.
@intrinsic("file.write_bytes_atomic")
pub fn write_bytes_atomic(path: text, content: array<u8>, replace: bool) -> Result<bool, text>

# Append text to a file
@intrinsic("file.append_text")
pub fn append_text(path: text, content: text) -> Result<bool, text>

# Delete a file
@intrinsic("file.delete")
pub fn delete(path: text) -> Result<bool, text>

# Copy a file
@intrinsic("file.copy")
pub fn copy(source: text, dest: text) -> Result<bool, text>

# Move/rename a file
@intrinsic("file.move")
pub fn move(source: text, dest: text) -> Result<bool, text>

# Get file size in bytes
@intrinsic("file.size")
pub fn size(path: text) -> Result<i64, text>

# =============================================================================
# Native Handle Intrinsics (Private)
# =============================================================================

@intrinsic("file.handle_open")
fn handle_open(path: text, mode: i32) -> i64
end fn

@intrinsic("file.handle_read")
fn handle_read(handle: i64, buf: ptr<u8>, len: i32) -> i32
end fn

@intrinsic("file.handle_write")
fn handle_write(handle: i64, buf: ptr<u8>, len: i32) -> i32
end fn

@intrinsic("file.handle_flush")
fn handle_flush(handle: i64) -> i32
end fn

@intrinsic("file.handle_close")
fn handle_close(handle: i64) -> i32
end fn

@intrinsic("file.handle_seek")
fn handle_seek(handle: i64, offset: i64, origin: i32) -> i64
end fn

# =============================================================================
# File Struct (for file handles)
# =============================================================================

pub struct File
    handle: i64  # Opaque runtime-owned handle token
    is_open: bool
    readable: bool
    writable: bool
end struct

pub fn open_file(path: text) -> Result<File, text>
    let handle = handle_open(path, 0)
    if handle < 0 then
        return Result.Error("File not found or access denied: " + path)
    end if
    return Result.Ok(File {
        handle: handle,
        is_open: true,
        readable: true,
        writable: false
    })
end fn

pub fn create_file(path: text) -> Result<File, text>
    let handle = handle_open(path, 1)
    if handle < 0 then
        return Result.Error("Failed to create file: " + path)
    end if
    return Result.Ok(File {
        handle: handle,
        is_open: true,
        readable: false,
        writable: true
    })
end fn

pub fn append_file(path: text) -> Result<File, text>
    let handle = handle_open(path, 2)
    if handle < 0 then
        return Result.Error("Failed to open file for append: " + path)
    end if
    return Result.Ok(File {
        handle: handle,
        is_open: true,
        readable: false,
        writable: true
    })
end fn

pub fn open_read_write_file(path: text) -> Result<File, text>
    let handle = handle_open(path, 3)
    if handle < 0 then
        return Result.Error("Failed to open file for reading and writing: " + path)
    end if
    return Result.Ok(File {
        handle: handle,
        is_open: true,
        readable: true,
        writable: true
    })
end fn

impl File
    pub fn open(path: text) -> Result<File, text>
        return open_file(path)
    end fn

    pub fn create(path: text) -> Result<File, text>
        return create_file(path)
    end fn

    pub fn append(path: text) -> Result<File, text>
        return append_file(path)
    end fn

    pub fn open_read_write(path: text) -> Result<File, text>
        return open_read_write_file(path)
    end fn

end impl

# =============================================================================
# Stream Trait Implementations
# =============================================================================

impl Read for File
    fn read(mut self, buffer: ptr<u8>, len: i32) -> Result<i32, text>
        if not self.is_open then return Result.Error("Read: File not open") end if
        if not self.readable then return Result.Error("Read: File not open for reading") end if
        let handle = self.handle
        let res = handle_read(handle, buffer, len)
        if res < 0 then return Result.Error("Read failed") end if
        return Result.Ok(res)
    end fn
end impl

impl Write for File
    fn write(mut self, buffer: ptr<u8>, len: i32) -> Result<i32, text>
        if not self.is_open then return Result.Error("Write: File not open") end if
        if not self.writable then return Result.Error("Write: File not open for writing") end if
        let handle = self.handle
        let res = handle_write(handle, buffer, len)
        if res < 0 then return Result.Error("Write failed") end if
        return Result.Ok(res)
    end fn

    fn flush(mut self) -> Result<bool, text>
        if not self.is_open then return Result.Error("Flush: File not open") end if
        if not self.writable then return Result.Error("Flush: File not open for writing") end if
        let res = handle_flush(self.handle)
        if res < 0 then return Result.Error("Flush failed") end if
        return Result.Ok(true)
    end fn
end impl

impl Seek for File
    fn seek(mut self, offset: i64, origin: i32) -> Result<i64, text>
        if not self.is_open then return Result.Error("Seek: File not open") end if
        let pos = handle_seek(self.handle, offset, origin)
        if pos < 0 then return Result.Error("Seek failed") end if
        return Result.Ok(pos)
    end fn
end impl

impl Close for File
    fn close(mut self) -> Result<bool, text>
        if self.is_open then
            let res = handle_close(self.handle)
            if res < 0 then return Result.Error("Close failed") end if
            self.is_open = false
        end if
        return Result.Ok(true)
    end fn
end impl
