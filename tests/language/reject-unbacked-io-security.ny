module ny.tests.reject_unbacked_io_security

import ny.lang.io.*
import ny.lang.security.*

# A declaration with no runtime provider must be rejected at compile time
# rather than accepted and then failing at the point of use. Compression,
# hashing and encryption are still designs under experimental/ with no
# intrinsic behind them, so each of these is undefined.
#
# `random_bytes` was on this list until nil.std.security.random gained an
# entropy intrinsic. It is now backed, and the fixture that covers it is
# tests/matrix/execution/entropy.matrix. It was removed from here rather than
# left to fail, because the thing this test guards is the gap between a
# declaration and a provider, and for that call there is no longer a gap.
fn main() -> i32
    let bytes: vec<u8> = vec<u8>.new()
    gzip_compress(bytes)
    sha256("data")
    aes_encrypt(bytes, bytes)
    return 0
end fn
