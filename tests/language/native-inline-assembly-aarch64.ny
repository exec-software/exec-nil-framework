// AArch64 counterpart of native-inline-assembly.ny, with identical first outputs.
let* evaluation_count: i64 = 0

fn next_input() -> i64
    evaluation_count = evaluation_count + 1
    return evaluation_count
end fn

fn swap_bytes(value: i64) -> i64
    let mut result: i64 = 0
    asm "aarch64", "mov $0, $1\n\trev $0, $0" (value) -> result
    end asm
    return result
end fn

fn main() -> i32
    asm "aarch64", "nop" () end asm
    println(swap_bytes(72623859790382856 as i64))
    let mut rotated: i64 = 0
    asm "aarch64", "ror $0, $1, #56" (3 as i64) -> rotated end asm
    println(rotated)
    let mut combined: i64 = 0
    asm "aarch64", "eor $0, $1, $2" (85 as i64, 15 as i64) -> combined end asm
    println(combined)
    // Inputs are evaluated exactly once, left to right, before assembly runs.
    asm "aarch64", "lsl $0, $1, #8; orr $0, $0, $2" (next_input(), next_input()) -> combined end asm
    println(combined)
    println(evaluation_count)
    asm "aarch64", "add $0, $1, $2" (combined, 1 as i64) -> combined end asm
    println(combined)
    // Constants use ordinary i64 inputs, not assembler pseudo-instructions.
    asm "aarch64", "mov $0, $1" (-9223372036854775808 as i64) -> combined end asm
    println(combined)
    asm "aarch64", "mov $0, $1" (9223372036854775807 as i64) -> combined end asm
    println(combined)
    asm "aarch64", "eor $0, $1, $1; mvn $0, $0; asr $0, $0, #63" (0 as i64) -> combined end asm
    println(combined)
    asm "aarch64", "lsl $0, $1, #63; lsr $0, $0, #63" (1 as i64) -> combined end asm
    println(combined)
    asm "aarch64", "ror $0, $1, #1; neg $0, $0; sub $0, $0, $2; and $0, $0, $3" (8 as i64, 1 as i64, 255 as i64) -> combined end asm
    println(combined)
    if true then
        let mut combined: i64 = 41
        asm "aarch64", "add $0, $1, $2" (combined, 1 as i64) -> combined end asm
        println(combined)
    end if
    println(combined)
    // Keep eight distinct inputs live across an early output write.
    asm "aarch64", "mov $0,$8; add $0,$0,$1; add $0,$0,$2; add $0,$0,$3; add $0,$0,$4; add $0,$0,$5; add $0,$0,$6; add $0,$0,$7" (1 as i64, 2 as i64, 3 as i64, 4 as i64, 5 as i64, 6 as i64, 7 as i64, 8 as i64) -> combined end asm
    println(combined)
    asm "aarch64", "lsr $0,$1,#63" (-9223372036854775808 as i64) -> combined end asm
    println(combined)
    asm "aarch64", "asr $0,$1,#63" (-9223372036854775808 as i64) -> combined end asm
    println(combined)
    asm "aarch64", "lsl $0,$1,#0; lsr $0,$0,#0; asr $0,$0,#0; ror $0,$0,#0" (-42 as i64) -> combined end asm
    println(combined)
    asm "aarch64", "add $0,$1,$2" (9223372036854775807 as i64, 1 as i64) -> combined end asm
    println(combined)
    asm "aarch64", "sub $0,$1,$2" (-9223372036854775808 as i64, 1 as i64) -> combined end asm
    println(combined)
    asm "aarch64", "ror $0,$1,#63" (3 as i64) -> combined end asm
    println(combined)
    return 0
end fn
