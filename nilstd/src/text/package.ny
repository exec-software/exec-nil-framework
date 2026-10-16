@pragma once
# =============================================================================
# ny.lang.text — Text Processing Package
# =============================================================================
# String manipulation, building, encoding, and regular expressions.
# Mirrors C# System.Text.
#
# Provides:
#   - String extensions: trim, split, starts_with, ends_with, pad (string.ny)
#   - StringBuilder: efficient mutable string building (string_builder.ny)
#   - String utilities: format, join, parse helpers (string_utils.ny)
#   - Regex: match, replace, split, capture groups (regex.ny)
#
# Cross-refs:
#   ny.lang.core  — text primitive type and extensions
#   ny.lang.io    — StreamReader/Writer use text encoding
# =============================================================================

package nil.std.text

import nil.std.text.string_utils
# Experimental: string_builder, regex
