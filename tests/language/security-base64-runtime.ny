module ny.tests.security_base64_runtime

import ny.lang.collections
import ny.lang.core.*
import ny.lang.security.*

fn main() -> i32
    let mut data: vec<u8> = vec<u8>.new()
    data.push(0)
    data.push(1)
    data.push(2)
    data.push(253)
    data.push(254)
    data.push(255)
    println(encode(data))
    println(hex_encode(data))

    match decode("AAEC/f7/")
        Result.Error(_) -> return 10
        Result.Ok(decoded) ->
            println(decoded.length())
            println(decoded.items[0])
            println(decoded.items[2])
            println(decoded.items[3])
            println(decoded.items[5])
    end match

    let mut one: vec<u8> = vec<u8>.new()
    one.push(77)
    println(encode(one))
    let mut two: vec<u8> = vec<u8>.new()
    two.push(77)
    two.push(97)
    println(encode(two))

    match decode("")
        Result.Error(_) -> return 11
        Result.Ok(empty) -> println(empty.length())
    end match
    match decode("A===")
        Result.Ok(_) -> return 12
        Result.Error(_) -> println("padding-error")
    end match
    match decode("AB==")
        Result.Ok(_) -> return 13
        Result.Error(_) -> println("pad-bits-error")
    end match
    match decode("AA=A")
        Result.Ok(_) -> return 14
        Result.Error(_) -> println("misplaced-padding-error")
    end match
    match decode("AA?=")
        Result.Ok(_) -> return 15
        Result.Error(_) -> println("alphabet-error")
    end match
    match decode("AAA")
        Result.Ok(_) -> return 16
        Result.Error(_) -> println("length-error")
    end match
    return 0
end fn
