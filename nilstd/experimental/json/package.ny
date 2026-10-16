@pragma once
# =============================================================================
# ny.lang.json — JSON Package
# =============================================================================
# High-performance JSON serialization and deserialization.
# Mirrors C# System.Text.Json.
#
# Provides:
#   - JsonValue: parse, serialize, navigate JSON trees (json.ny)
#   - JsonObject, JsonArray, JsonNull, JsonBool (json.ny)
#   - to_json / from_json trait-based serialization (json.ny)
#
# Cross-refs:
#   ny.lang.core — text, Result<T,E>
#   ny.lang.net  — HTTP responses often contain JSON payloads
# =============================================================================

package nil.std.json

# Experimental: json
