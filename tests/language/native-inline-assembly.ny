// A register-only transform: no external state or instruction-set extensions.
let* evaluation_count: i64 = 0

fn next_input() -> i64
    evaluation_count = evaluation_count + 1
    return evaluation_count
end fn

fn swap_bytes(value: i64) -> i64
    let mut result: i64 = 0
    asm "x86_64", "movq $1, $0\n\tbswapq $0" (value) -> result
    end asm
    return result
end fn

fn main() -> i32
    asm "x86_64", "nop" () end asm
    println(swap_bytes(72623859790382856 as i64))
    let mut rotated: i64 = 0
    asm "x86_64", "movq $1, $0; rolq $$8, $0" (3 as i64) -> rotated
    end asm
    println(rotated)
    let mut combined: i64 = 0
    asm "x86_64", "movq $1, $0; xorq $2, $0" (85 as i64, 15 as i64) -> combined
    end asm
    println(combined)
    // Inputs must be evaluated exactly once and in source order.
    asm "x86_64", "movq $1, $0; shlq $$8, $0; orq $2, $0" (next_input(), next_input()) -> combined
    end asm
    println(combined)
    println(evaluation_count)
    // The output can also be supplied as an independent input value.
    asm "x86_64", "movq $1, $0; addq $$1, $0" (combined) -> combined end asm
    println(combined)
    asm "x86_64", "movq $$-9223372036854775808, $0" () -> combined end asm
    println(combined)
    asm "x86_64", "movq $$9223372036854775807, $0" () -> combined end asm
    println(combined)
    asm "x86_64", "xorq $0, $0; notq $0; sarq $$63, $0" () -> combined end asm
    println(combined)
    asm "x86_64", "movq $$1, $0; salq $$63, $0; shrq $$63, $0" () -> combined end asm
    println(combined)
    asm "x86_64", "movq $$8, $0; rorq $$1, $0; negq $0; subq $$1, $0; andq $$255, $0" () -> combined end asm
    println(combined)
    // Both operands and the output must resolve the innermost lexical local.
    if true then
        let mut combined: i64 = 41
        asm "x86_64", "movq $1, $0; addq $$1, $0" (combined) -> combined end asm
        println(combined)
    end if
    println(combined)
    return 0
end fn
