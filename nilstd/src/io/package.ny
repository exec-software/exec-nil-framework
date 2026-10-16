@pragma once
# =============================================================================
# ny.lang.io — I/O Package
# =============================================================================
# File system, streaming, path, and console I/O.
#
# Provides:
#   - Console I/O: write, writeln, read_line, write_error, flush (console.ny)
#   - File: checked whole-file operations, bounded regular-file input, atomic
#     byte publication, and opaque-handle streams (file.ny)
#   - Stream: Read/Write/Seek/Close traits and checked stream_copy (stream.ny)
#   - Path: lexical path inspection and transformation (path.ny)
#   - Binary: bounded little/big-endian byte-array reads and writes (binary.ny)
#   - Varint: canonical base-128 u32 and ZigZag i64 encoding (varint.ny)
#   - Checksum: bounded and incremental CRC-32 (checksum.ny)
#
# The interpreter backs the file, stream, path, and console declarations.
# Their native route is not certified yet. The unbuilt compression prototype
# is preserved under experimental/ and is not part of the active SDK.
#
# Cross-refs:
#   ny.lang.core  — Result<T,E>, IDisposable
#   ny.lang.text  — StreamReader/Writer use text encoding
# =============================================================================

package nil.std.io

import nil.std.io.console
import nil.std.io.file
# Experimental: file_system
import nil.std.io.stream
import nil.std.io.path
import nil.std.io.binary
import nil.std.io.varint
import nil.std.io.checksum
