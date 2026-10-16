module ny.tests.reject_unbacked_io_security

import ny.lang.io.*
import ny.lang.security.*

fn main() -> i32
    let bytes: vec<u8> = vec<u8>.new()
    gzip_compress(bytes)
    sha256("data")
    random_bytes(16)
    aes_encrypt(bytes, bytes)
    return 0
end fn
