@schema
@pragma once
module nil.std.text

import nil.std.core

# Regular-expression intrinsics

@intrinsic("regex.match")
pub fn match(text: text, pattern: text) -> bool
end fn

@intrinsic("regex.replace")
pub fn replace(text: text, pattern: text, replacement: text) -> text
end fn
