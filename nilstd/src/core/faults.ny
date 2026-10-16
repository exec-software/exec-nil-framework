@schema
@pragma once

module nil.std.core

# =============================================================================
# Core Fault Types
# =============================================================================


# Base context for AI debugging
pub struct AIContext
    pub operation: text
    pub intent: text
    pub variables: array<text>
    pub likely_causes: array<text>
    pub suggested_fixes: array<text>
end struct

pub struct Fault
    pub message: text
    pub code: text
    pub file: text
    pub line: i32
    pub column: i32
    pub function_name: text
end struct

# =============================================================================
# Specific Fault Types
# =============================================================================

pub struct AssertionFault
    pub message: text
    pub file: text
    pub line: i32
    pub method: text
    pub expected: text
    pub actual: text
end struct

impl AssertionFault
    pub fn new(msg: text, file: text, line: i32, method: text) -> AssertionFault
        return AssertionFault {
            message: msg,
            file: file,
            line: line,
            method: method,
            expected: "",
            actual: ""
        }
    end fn

    pub fn to_string(self) -> text
        return "AssertionFault: " + self.message + " at " + self.file + ":" + Convert.i32_to_text(self.line) + " (" + self.method + ")"
    end fn
end impl

pub struct IoFault
    pub message: text
    pub path: text
    pub operation: text
end struct
