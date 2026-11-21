module validation.choice_text_fields

import nil.std.core.option
import nil.std.core.result

# Inferred String payloads and declared text fields must select the same exact
# enum schema, including when the schema is nested in a record or class field.
struct Slot
    optional_text: Option<text>
    result_text: Result<text, text>
end struct

class Holder
    optional_text: Option<text>
    result_text: Result<text, text>

    pub fn read_optional(self) -> text
        return option_text(self.optional_text)
    end fn

    pub fn read_result(self) -> text
        return result_payload(self.result_text)
    end fn
end class

fn option_text(input: Option<text>) -> text
    match input
        Option.Some(found) -> return found
        Option.None -> return "none"
    end match
end fn

fn result_payload(input: Result<text, text>) -> text
    match input
        Result.Ok(found) -> return found
        Result.Error(failure) -> return failure
    end match
end fn

fn main() -> i32
    let slot_value = Slot {
        optional_text: Option.Some("record:alpha"),
        result_text: Result.Ok("record:ok")
    }
    println(option_text(slot_value.optional_text))
    println(result_payload(slot_value.result_text))
    slot_value.optional_text = Option.None
    slot_value.result_text = Result.Error("record:error")
    println(option_text(slot_value.optional_text))
    println(result_payload(slot_value.result_text))

    let holder_value = Holder {
        optional_text: Option.Some("class:alpha"),
        result_text: Result.Ok("class:ok")
    }
    println(holder_value.read_optional())
    println(holder_value.read_result())
    holder_value.optional_text = Option.None
    holder_value.result_text = Result.Error("class:error")
    println(holder_value.read_optional())
    println(holder_value.read_result())
    return 0
end fn
