@pragma once
# =============================================================================
# ny.lang.security — Security Encoding Package
# =============================================================================
# Portable binary-to-text encoding APIs.
#
# Provides:
#   - Base64: portable encode and strict decode (base64.ny)
#   - Hex: portable lowercase encoding (encoding.ny)
#
# Unbuilt hash, crypto, and secure-random design declarations are preserved
# under experimental/. Their native shims are not ABI-safe Ny providers and do
# not enter nilstd or nillib.
#
# Cross-refs:
#   ny.lang.core — Result<T,E>, byte/u8 types
#   ny.lang.collections — vector-backed encoded byte values
# =============================================================================

package nil.std.security

import nil.std.security.base64
import nil.std.security.encoding
