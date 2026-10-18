# Exact values were independently calculated as seed * 48271^n mod 2147483647.
import nil.std.math
import nil.std.core

fn require(ok: bool, message: text)
    if not ok then panic(message) end if
end fn

fn check_seed(input: i64, first: i32, second: i32)
    let stream = random_generator.new(input)
    require(stream.next_i32() == first and stream.next_i32() == second, "constructor seed normalization")
    stream.reseed(input)
    require(stream.next_i32() == first and stream.next_i32() == second, "reseed normalization")
    seed(input)
    require(random_i32() == first and random_i32() == second, "global seed normalization")
end fn

fn check_float_range(lower: f64, upper: f64, input: i64)
    let stream = random_generator.new(input)
    let reference = stream.clone()
    let first = reference.next_i32()
    let second = reference.next_i32()
    let actual = stream.range_f64(lower, upper)
    require(is_finite(actual) and actual >= lower and actual < upper, "instance finite half-open interval")
    require(stream.next_i32() == second, "instance float range consumes one draw")
    seed(input)
    let shared = random_range_f64(lower, upper)
    require(is_finite(shared) and shared >= lower and shared < upper, "global finite half-open interval")
    require(shared == actual and random_i32() == second, "global float range matches instance and advances once")
end fn

fn main() -> i32
    require(random_i32() == 595905495 and random_i32() == 1558181227, "default global seed remains 12345")
    let stream = random_generator.new(1)
    let expected: array<i32> = [48271, 182605794, 1291394886, 1914720637, 2078669041, 407355683, 1105902161, 854716505]
    seed(1)
    for index in range(expected.len())
        let actual = stream.next_i32()
        require(actual == expected[index] and random_i32() == expected[index], "independent MINSTD sequence oracle")
        println(actual)
    end for

    let maximum: i64 = 9223372036854775807
    let minimum: i64 = -maximum - 1
    check_seed(0, 48271, 182605794)
    check_seed(-1, 2147435376, 1964877853)
    check_seed(2147483647, 48271, 182605794)
    check_seed(-2147483647, 48271, 182605794)
    check_seed(minimum, 2147387105, 1782272059)
    check_seed(maximum, 48271, 182605794)
    check_seed(12345, 595905495, 1558181227)
    stream.reseed(1)
    require(not stream.next_bool() and stream.next_bool(), "instance boolean parity sequence")
    require(stream.next_i32() == 1291394886, "instance boolean consumes one draw")
    seed(1)
    require(not random_bool() and random_bool() and random_i32() == 1291394886, "global boolean parity sequence")
    stream.reseed(1)
    require(stream.next_f64() == 48271.0 / 2147483647.0, "instance floating sequence oracle")
    require(stream.next_i32() == 182605794, "instance floating consumes one draw")
    seed(1)
    require(random_f64() == 48271.0 / 2147483647.0 and random_i32() == 182605794, "global floating sequence oracle")
    stream.reseed(247665088)
    let largest = stream.next_f64()
    require(largest == 2147483646.0 / 2147483647.0 and largest < 1.0, "unit interval excludes one")
    stream.reseed(1899818559)
    require(stream.next_f64() == 1.0 / 2147483647.0, "unit interval excludes zero")
    stream.state = 0
    require(stream.next_i32() == 48271, "externally written zero normalizes on draw")
    stream.state = minimum
    let raw_copy = stream.clone()
    require(raw_copy.state == minimum, "clone copies unnormalized state exactly")
    require(stream.next_i32() == 2147387105 and raw_copy.next_i32() == 2147387105, "externally written minimum normalizes on draw")
    stream.state = maximum
    require(stream.next_i32() == 48271 and raw_copy.state == 2147387105, "externally written maximum and independent clone")
    println("seed normalization and scalar draws")

    let first = random_generator.new(1)
    let other = random_generator.new(12345)
    seed(-1)
    require(first.next_i32() == 48271 and other.next_i32() == 595905495 and random_i32() == 2147435376, "independent interleaved streams")
    let alias = first
    require(alias.next_i32() == 182605794 and first.next_i32() == 1291394886, "ordinary assignment aliases state")
    let copy = first.clone()
    require(first.next_i32() == 1914720637 and copy.next_i32() == 1914720637, "clone resumes at copied state")
    require(first.next_i32() == 2078669041 and copy.state == 1914720637, "clone advances independently")
    copy.reseed(0)
    require(copy.next_i32() == 48271 and alias.next_i32() == 407355683, "clone reseed preserves original and alias")
    require(other.next_i32() == 1558181227 and random_i32() == 1964877853, "instance draws leave other and global states unchanged")
    println("independent instances, aliases and clones")

    stream.reseed(1)
    require(stream.range_i32(-5, -5) == -5 and stream.next_i32() == 48271, "equal integer bounds consume no state")
    seed(1)
    require(random_range(-5, -5) == -5 and random_i32() == 48271, "global equal integer bounds consume no state")
    stream.reseed(1)
    require(stream.range_i32(-2147483648, 2147483647) == -1964974395, "full-width bounded sequence oracle")
    require(stream.next_i32() == 1291394886, "ordinary integer range consumes two draws")
    seed(1)
    require(random_range(-2147483648, 2147483647) == -1964974395 and random_i32() == 1291394886, "global full-width sequence oracle")
    stream.reseed(1)
    require(stream.range_i32(-10, 10) == 10, "inclusive upper integer endpoint")
    stream.reseed(12345)
    require(stream.range_i32(-10, 10) == -10, "inclusive lower integer endpoint")
    stream.reseed(247665088)
    require(stream.range_i32(0, 999999999) == 413697152, "rejected first pair selects next pair")
    require(stream.next_i32() == 232763010, "rejection consumes four draws")
    seed(247665088)
    require(random_range(0, 999999999) == 413697152 and random_i32() == 232763010, "global rejection sequence oracle")
    println("integer bounds and rejection")

    stream.reseed(1)
    require(stream.range_f64(-7.0, -7.0) == -7.0 and stream.next_i32() == 48271, "equal floating bounds consume no state")
    seed(1)
    require(random_range_f64(-7.0, -7.0) == -7.0 and random_i32() == 48271, "global equal floating bounds consume no state")
    stream.reseed(1)
    require(atan2(stream.range_f64(-0.0, 0.0), -1.0) < 0.0, "equal floating bounds retain lower signed zero")
    require(stream.next_i32() == 48271, "signed zero interval consumes no state")
    seed(1)
    require(atan2(random_range_f64(-0.0, 0.0), -1.0) < 0.0 and random_i32() == 48271, "global signed zero interval")
    let huge = pow(2.0, 1023.0) * 1.75
    let tiny = pow(2.0, -1074.0)
    stream.reseed(1)
    require(stream.range_f64(-2.0, 3.0) == -2.0 + (48271.0 / 2147483647.0) * 5.0, "ordinary floating formula compatibility")
    stream.reseed(1)
    let scaled = stream.range_f64(-huge, huge) / huge
    require(abs(scaled - (2.0 * (48271.0 / 2147483647.0) - 1.0)) <= 0.000000000000001, "overflowing span retains sampled direction")
    let inputs: array<i32> = [1, 12345, -1, 247665088]
    for index in range(inputs.len())
        let input = inputs[index]
        check_float_range(-2.0, 3.0, input)
        check_float_range(-huge, huge, input)
        check_float_range(huge / 2.0, huge, input)
        check_float_range(-huge, -huge / 2.0, input)
        check_float_range(1.0, 1.0000000000000002, input)
        check_float_range(-1.0000000000000002, -1.0, input)
        check_float_range(0.0, tiny, input)
        check_float_range(-tiny, 0.0, input)
        check_float_range(-tiny, tiny, input)
    end for
    stream.reseed(247665088)
    require(stream.range_f64(1.0, 1.0000000000000002) == 1.0, "rounded upper endpoint returns lower")
    println("floating bounds and state consumption")
    return 0
end fn
