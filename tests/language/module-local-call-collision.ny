module later_module

# This deliberately collides with local_owner::select_value. Import order must
# never make an imported helper shadow a same-module call.
pub fn select_value() -> text
    return "wrong module"
end fn
