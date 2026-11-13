@schema
@pragma once

# =============================================================================
# nil.std.security — cryptographically secure random values
# =============================================================================
#
# The only unpredictable source in the standard library.
#
# nil.std.math is MINSTD, a 31-bit linear congruential generator whose global
# stream starts at the seed 12345, and it says in its own comments that it is
# not cryptographic. Two draws from it reveal the whole stream. That is the
# right generator for a simulation, a shuffle or a jittered retry, and the
# wrong one for anything a user holds: a session identifier, a CSRF token, a
# password-reset link, a nonce. Those are bearer credentials, and one drawn
# from MINSTD is one anyone can compute.
#
# Until these intrinsics existed there was no alternative — no hash, no HMAC,
# no random here, and /dev/urandom unreadable through the file intrinsics,
# which refuse a character device by design. A library that needed a
# credential had to invent something predictable or drop the feature.
#
# These ask the operating system and do nothing else. No userspace pool, no
# seeding, no state to get wrong, and no fallback to a weak source: if the
# kernel cannot provide entropy the answer is an error, because quietly
# returning something guessable is how this goes wrong in a way nobody
# notices until it matters.
#
# A single draw is limited to 1024 bytes. A key is 32 and a generous token is
# 64; asking for a megabyte is a program that wants a stream cipher seeded
# from here instead.
#
# Interpreter-backed. The native route does not carry these yet.

module nil.std.security

import nil.std.core

# Random bytes as lower-case hex, two characters per byte.
#
# The one to reach for. A credential wants to be text anyway, and hex
# survives a cookie, a URL, a header and a log line with no further escaping.
# It is also the form Matrix can use, since a Matrix page cannot index an Ny
# byte array.
#
# 16 bytes (32 characters) is a reasonable session identifier; 32 bytes is
# generous and still short enough to read.
@intrinsic("random.hex")
pub fn random_hex(byte_count: i64) -> Result<text, text>

# Random bytes as they are.
#
# Returned as text because that is what crosses into Matrix, but the content
# is arbitrary bytes and is not valid UTF-8 in general. Read it with
# nil.std.text's byte_at, never with substring or length-as-characters.
# Prefer random_hex unless the raw bytes are what is wanted.
@intrinsic("random.bytes")
pub fn random_bytes(byte_count: i64) -> Result<text, text>

# A uniform whole number in [0, bound).
#
# By rejection sampling, not by remainder. Taking a draw modulo the bound
# skews towards the low values whenever the bound does not divide the range
# evenly — a bias small enough never to be noticed and large enough to matter
# to anything that was counting on uniformity.
@intrinsic("random.below")
pub fn random_below(bound: i64) -> Result<i64, text>
