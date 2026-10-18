module validation.qualified_enums

import nil.std.core
import ny.lang.core
namespace legacy = ny.lang.core

let comparison_limit: i32 = 7

struct Limits
    value: i32
end struct

impl Limits
    fn get(self) -> i32
        return self.value
    end fn
end impl

fn comparison_value() -> i32
    return 7
end fn

enum Signal
    Ready(i32)
    Idle
end enum

fn main() -> i32
    let selected: Option<i32> = Option.Some(9)
    match selected
        nil::std::core::Option::None -> println(0)
        nil::std::core::Option::Some(value) -> println(value)
    end match
    match selected
        ny::lang::core::Option::Some(value) when value > 20 -> println(0)
        ny::lang::core::Option::Some(value) -> println(value + 1)
        _ -> println(0)
    end match
    println(match selected { Option.Some(value) -> value + 2; _ -> 0; })
    println(match selected { nil::std::core::Option::Some(value) -> value + 3; _ -> 0; })
    println(match selected { ny::lang::core::Option::Some(value) -> value + 4; _ -> 0; })

    let empty: Option<i32> = Option.None
    match empty
        nil.std.core.Option.Some(value) -> println(0)
        nil.std.core.Option.None -> println(14)
    end match
    println(match empty { ny::lang::core::Option::None -> 15; _ -> 0; })
    let qualified: nil::std::core::Option<i32> = nil::std::core::Option::Some(16)
    match qualified
        Option.Some(value) -> println(value)
        _ -> println(0)
    end match
    let legacy: ny::lang::core::Option<i32> = ny::lang::core::Option::Some(17)
    match legacy
        nil::std::core::Option::Some(value) -> println(value)
        _ -> println(0)
    end match

    let result: Result<f64, text> = Result.Ok(-2.375)
    match result
        nil::std::core::Result::Error(message) -> println(message)
        nil::std::core::Result::Ok(value) -> println(value == -2.375)
    end match
    let failed: Result<f64, text> = Result.Error("legacy error")
    println(match failed { ny::lang::core::Result::Error(message) -> message; _ -> "wrong"; })

    let signal: Signal = Signal.Ready(18)
    match signal
        validation::qualified_enums::Signal::Ready(value) -> println(value)
        _ -> println(0)
    end match
    let idle: Signal = validation::qualified_enums::Signal::Idle
    println(match idle { Signal.Idle -> 19; _ -> 0; })
    match selected
        legacy::Option::Some(value) -> println(value + 11)
        _ -> println(0)
    end match
    println(match selected { legacy::Option::Some(value) -> value + 12; _ -> 0; })
    let limits = Limits { value: 7 }
    println(match 7 { limits.value -> 22; _ -> 0; })
    println(match 7 { validation::qualified_enums::comparison_limit -> 23; _ -> 0; })
    println(match 7 { limits.get() -> 24; _ -> 0; })
    println(match 7 { validation::qualified_enums::comparison_value() -> 25; _ -> 0; })
    println(match 7 { comparison_value() -> 26; _ -> 0; })
    return 0
end fn
