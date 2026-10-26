@pragma once
module nil.std.nildata

import nil.std.core

# Values retain exact JSON number tokens and have no runtime handles. Every
# operation validates encoded input, including records constructed by callers.
# Mutations return a new Value; the original value remains unchanged.
pub struct Value
    encoded: text
end struct

@intrinsic("nildata.request")
fn _nildata_request(operation: text, source: text, argument: text, index: i64) -> array<text>
end fn

fn _data_text(operation: text, source: text, argument: text, index: i64) -> Result<text, text>
    let reply = _nildata_request(operation, source, argument, index)
    if reply.len() != 2 then return Result.Error("invalid NILData runtime reply") end if
    if reply[0] != "" then return Result.Error(reply[0]) end if
    return Result.Ok(reply[1])
end fn

fn _data_value(operation: text, source: text, argument: text, index: i64) -> Result<Value, text>
    let reply = _data_text(operation, source, argument, index)
    if reply.is_error() then return Result.Error(reply.unwrap_error()) end if
    return Result.Ok(Value { encoded: reply.unwrap() })
end fn

fn _data_bool(operation: text, source: text, argument: text) -> Result<bool, text>
    let reply = _data_text(operation, source, argument, 0)
    if reply.is_error() then return Result.Error(reply.unwrap_error()) end if
    let result = reply.unwrap()
    if result == "true" then return Result.Ok(true) end if
    if result == "false" then return Result.Ok(false) end if
    return Result.Error("invalid NILData boolean reply")
end fn

# loads accepts only versioned NILData. Legacy formats require an explicitly
# named importer; neither numeric-looking keys nor scalar strings are guessed.
pub fn loads(source: text) -> Result<Value, text>
    return _data_value("load", source, "", 0)
end fn

pub fn dumps(value: Value) -> Result<text, text>
    return _data_text("dump", value.encoded, "", 0)
end fn

pub fn from_json(source: text) -> Result<Value, text>
    return _data_value("from_json", source, "", 0)
end fn

pub fn to_json(value: Value) -> Result<text, text>
    return _data_text("to_json", value.encoded, "", 0)
end fn

pub fn import_nydata(source: text) -> Result<Value, text>
    return _data_value("legacy", source, "", 0)
end fn

pub fn import_nydata_compiler(source: text) -> Result<Value, text>
    return _data_value("legacy_compiler", source, "", 0)
end fn

# kind returns null, boolean, integer, decimal, text, array, or object.
pub fn kind(value: Value) -> Result<text, text>
    return _data_text("kind", value.encoded, "", 0)
end fn

# No floating-point conversion occurs: precision, exponents, and signed zero
# survive parsing, serialization, and functional updates unchanged.
pub fn numeric_text(value: Value) -> Result<text, text>
    return _data_text("number", value.encoded, "", 0)
end fn

pub fn as_i64(value: Value) -> Result<i64, text>
    let number = numeric_text(value)
    if number.is_error() then return Result.Error(number.unwrap_error()) end if
    return Convert.try_to_i64(number.unwrap())
end fn

# Encoded U+0000 survives round trips. Extraction into runtime text rejects
# embedded NUL on every route rather than truncating it in native programs.
pub fn text_value(value: Value) -> Result<text, text>
    return _data_text("text", value.encoded, "", 0)
end fn

pub fn bool_value(value: Value) -> Result<bool, text>
    return _data_bool("boolean", value.encoded, "")
end fn

pub fn size(value: Value) -> Result<i64, text>
    let count = _data_text("size", value.encoded, "", 0)
    if count.is_error() then return Result.Error(count.unwrap_error()) end if
    return Convert.try_to_i64(count.unwrap())
end fn

pub fn new_object() -> Result<Value, text>
    return _data_value("object", "", "", 0)
end fn

pub fn new_array() -> Result<Value, text>
    return _data_value("array", "", "", 0)
end fn

pub fn null_value() -> Result<Value, text>
    return _data_value("null", "", "", 0)
end fn

pub fn string_value(value: text) -> Result<Value, text>
    return _data_value("string", "", value, 0)
end fn

pub fn integer_value(number: text) -> Result<Value, text>
    return _data_value("integer", "", number, 0)
end fn

pub fn decimal_value(number: text) -> Result<Value, text>
    return _data_value("decimal", "", number, 0)
end fn

pub fn boolean_value(value: bool) -> Result<Value, text>
    if value then return _data_value("flag", "", "true", 0) end if
    return _data_value("flag", "", "false", 0)
end fn

pub fn object_has(value: Value, key: text) -> Result<bool, text>
    return _data_bool("has", value.encoded, key)
end fn

pub fn object_get(value: Value, key: text) -> Result<Option<Value>, text>
    let present = object_has(value, key)
    if present.is_error() then return Result.Error(present.unwrap_error()) end if
    if not present.unwrap() then return Result.Ok(Option.None) end if
    let found = _data_value("get", value.encoded, key, 0)
    if found.is_error() then return Result.Error(found.unwrap_error()) end if
    return Result.Ok(Option.Some(found.unwrap()))
end fn

pub fn object_set(value: Value, key: text, replacement: Value) -> Result<Value, text>
    let encoded_key = string_value(key)
    if encoded_key.is_error() then return Result.Error(encoded_key.unwrap_error()) end if
    let pair = "[" + encoded_key.unwrap().encoded + "," + replacement.encoded + "]"
    return _data_value("set", value.encoded, pair, 0)
end fn

# Enumeration uses deterministic sorted key order. Out-of-range
# positions are absent, while invalid values and wrong container kinds fail.
pub fn object_key(value: Value, index: i64) -> Result<Option<text>, text>
    let shape = kind(value)
    if shape.is_error() then return Result.Error(shape.unwrap_error()) end if
    if shape.unwrap() != "object" then return Result.Error("NILData object_key requires an object") end if
    if index < 0 then return Result.Error("NILData object key index must be nonnegative") end if
    let count = size(value)
    if count.is_error() then return Result.Error(count.unwrap_error()) end if
    if index >= count.unwrap() then return Result.Ok(Option.None) end if
    let key = _data_text("key", value.encoded, "", index)
    if key.is_error() then return Result.Error(key.unwrap_error()) end if
    return Result.Ok(Option.Some(key.unwrap()))
end fn

pub fn array_get(value: Value, index: i64) -> Result<Option<Value>, text>
    let shape = kind(value)
    if shape.is_error() then return Result.Error(shape.unwrap_error()) end if
    if shape.unwrap() != "array" then return Result.Error("NILData array_get requires an array") end if
    if index < 0 then return Result.Error("NILData array index must be nonnegative") end if
    let count = size(value)
    if count.is_error() then return Result.Error(count.unwrap_error()) end if
    if index >= count.unwrap() then return Result.Ok(Option.None) end if
    let item = _data_value("at", value.encoded, "", index)
    if item.is_error() then return Result.Error(item.unwrap_error()) end if
    return Result.Ok(Option.Some(item.unwrap()))
end fn

pub fn array_append(value: Value, item: Value) -> Result<Value, text>
    return _data_value("append", value.encoded, item.encoded, 0)
end fn

pub fn array_set(value: Value, index: i64, item: Value) -> Result<Value, text>
    return _data_value("replace", value.encoded, item.encoded, index)
end fn

pub fn array_remove(value: Value, index: i64) -> Result<Value, text>
    return _data_value("erase_at", value.encoded, "", index)
end fn

# Removing an absent member returns the same value content successfully.
pub fn object_remove(value: Value, key: text) -> Result<Value, text>
    return _data_value("erase_key", value.encoded, key, 0)
end fn
