import ny.lang.core.*

struct parcel
    value: i32
end struct

struct named_unwrap
    value: i32
end struct

impl named_unwrap
    fn unwrap(self) -> i32
        return self.value
    end fn
end impl

fn main() -> i32
    let present: Option<i32> = Option.Some(9)
    let missing: Option<i32> = Option.None
    let success: Result<i32, text> = Result.Ok(11)
    let failure: Result<i32, text> = Result.Error("failure")
    let wrapped_parcel: Option<parcel> = Option.Some(parcel { value: 17 })
    let ordinary: named_unwrap = named_unwrap { value: 23 }

    println(present.is_some())
    println(missing.is_none())
    println(present.unwrap())
    println(success.is_ok())
    println(failure.is_err())
    println(failure.is_error())
    println(success.unwrap())
    println(failure.unwrap_error())
    println(wrapped_parcel.unwrap().value)
    println(ordinary.unwrap())
    return 0
end fn
