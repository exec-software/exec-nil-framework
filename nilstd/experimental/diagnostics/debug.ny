@schema
@pragma once

module nil.std.diagnostics

###
 Breakpoint management and inspection tools.
###

# Trigger a breakpoint programmatically
@intrinsic("debug.break")
pub fn break_here()
end fn

# Print current stack trace to stderr
@intrinsic("debug.trace")
pub fn print_stack_trace()
end fn

# Check if debugger is attached
@intrinsic("debug.is_attached")
pub fn is_attached() -> bool
end fn

