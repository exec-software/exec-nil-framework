@pragma once
module nil.std.io

import nil.std.core

# Compile-only design declarations. The preserved C++ provider is not linked
# into the SDK and returns C++ vectors across a C ABI, which is not a supported
# Ny FFI contract. Do not use these functions as an executable API yet.

@interop("ml_io_compression_gzip_compress")
pub fn gzip_compress(data: vec<u8>) -> vec<u8>
end fn

@interop("ml_io_compression_gzip_decompress")
pub fn gzip_decompress(data: vec<u8>) -> vec<u8>
end fn
