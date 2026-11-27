#include "binary32.hpp"

#include <cfenv>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <utility>

namespace {
int failures = 0;
void check(bool condition, const char *message) {
  if (!condition) {
    if (failures < 20) std::cerr << message << '\n';
    ++failures;
  }
}

std::uint32_t float_bits(float value) {
  std::uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value) && std::numeric_limits<float>::is_iec559);
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void boundaries() {
  const std::pair<double, std::uint32_t> cases[] = {
      {0.0, 0}, {-0.0, 0x80000000U}, {1.0, 0x3f800000U}, {-1.0, 0xbf800000U},
      {16777217.0, 0x4b800000U}, {16777219.0, 0x4b800002U},
      {0x1.000001p0, 0x3f800000U}, {0x1.000003p0, 0x3f800002U},
      {0x1.0000010000001p0, 0x3f800001U}, {0x1.000000fffffffp0, 0x3f800000U},
      {0x1.fffffep127, 0x7f7fffffU}, {0x1.ffffffp127, 0x7f800000U},
      {0x1.fffffefffffffp127, 0x7f7fffffU}, {-0x1.ffffffp127, 0xff800000U},
      {0x1p-126, 0x00800000U}, {0x1.fffffcp-127, 0x007fffffU},
      {0x1.fffffep-127, 0x00800000U}, {0x1p-149, 1},
      {0x1p-150, 0}, {-0x1p-150, 0x80000000U}, {0x1.8p-149, 2},
      {0x1.0000000000001p-150, 1}, {-0x1p-1074, 0x80000000U},
      {std::numeric_limits<double>::max(), 0x7f800000U},
      {std::numeric_limits<double>::infinity(), 0x7f800000U},
      {-std::numeric_limits<double>::infinity(), 0xff800000U},
      {ny::binary32_detail::carrier_from_bits(0x7ff0000000000001ULL), 0x7fc00000U},
      {ny::binary32_detail::carrier_from_bits(0xfff8000000000042ULL), 0x7fc00000U},
  };
  for (const auto &[value, expected] : cases) {
    check(ny::binary32_from_f64(value) == expected, "f64 narrowing boundary mismatch");
    check(ny::binary32_is_canonical(ny::binary32_round(value)), "narrowed carrier is not canonical");
  }
  check(ny::binary32_detail::carrier_bits(ny::binary32_to_f64(0x80000000U)) ==
            0x8000000000000000ULL, "widening lost negative zero");
  check(ny::binary32_detail::carrier_bits(ny::binary32_to_f64(1)) ==
            0x36a0000000000000ULL, "smallest subnormal did not widen exactly");
  check(!ny::binary32_is_canonical(16777217.0), "unrounded double was accepted as binary32");
  check(ny::binary32_from_i64(std::numeric_limits<std::int64_t>::min()) == 0xdf000000U,
        "signed minimum conversion overflowed");
  check(ny::binary32_from_i64(std::numeric_limits<std::int64_t>::max()) == 0x5f000000U,
        "signed maximum conversion mismatch");
  check(ny::binary32_from_u64(std::numeric_limits<std::uint64_t>::max()) == 0x5f800000U,
        "unsigned maximum conversion mismatch");
  const auto midpoint = (std::int64_t{1} << 62) + (std::int64_t{1} << 38);
  check(ny::binary32_from_i64(midpoint) == 0x5e800000U &&
            ny::binary32_from_i64(midpoint + 1) == 0x5e800001U &&
            ny::binary32_from_i64(-midpoint - 1) == 0xde800001U,
        "integer narrowing double-rounded an even midpoint");
  check(ny::binary32_from_i64(midpoint + (std::int64_t{1} << 39) - 1) == 0x5e800001U,
        "integer narrowing double-rounded an odd midpoint");
}

void widening_roundtrips() {
  for (unsigned exponent = 0; exponent < 256; ++exponent)
    for (const auto fraction : {0U, 1U, 2U, 0x003fffffU, 0x00400000U, 0x007ffffeU, 0x007fffffU})
      for (const auto sign : {0U, 0x80000000U}) {
        const auto bits = sign | (exponent << 23) | fraction;
        const auto expected = exponent == 255 && fraction ? 0x7fc00000U : bits;
        check(ny::binary32_from_f64(ny::binary32_to_f64(bits)) == expected,
              "binary32 encoding did not widen and narrow exactly");
      }
}
} // namespace

int main() {
  const auto initial_rounding = std::fegetround();
  for (const auto mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    check(std::fesetround(mode) == 0, "could not exercise floating rounding mode");
    boundaries();
    widening_roundtrips();
  }
  check(std::fesetround(FE_TONEAREST) == 0, "could not set nearest rounding oracle");
  // Compare varied finite inputs to an independent hardware binary32 cast.
  // Explicit boundary tests above also exercise overflow and NaN policies.
  std::uint64_t state = 0x782ad1946f05cbe3ULL;
  for (unsigned sample = 0; sample < 100000; ++sample) {
    state ^= state << 13; state ^= state >> 7; state ^= state << 17;
    const auto exponent = 870U + static_cast<unsigned>((state >> 32) % 280);
    const auto bits = (state & 0x800fffffffffffffULL) | (std::uint64_t{exponent} << 52);
    volatile double input = ny::binary32_detail::carrier_from_bits(bits);
    volatile float rounded = static_cast<float>(input);
    check(ny::binary32_from_f64(input) == float_bits(rounded), "binary64 oracle mismatch");
    volatile std::int64_t integer = static_cast<std::int64_t>(state >> 1);
    volatile float integer_rounded = static_cast<float>(integer);
    check(ny::binary32_from_i64(integer) == float_bits(integer_rounded), "integer oracle mismatch");
  }
  if (initial_rounding != -1) std::fesetround(initial_rounding);
  if (failures) std::cerr << failures << " binary32 checks failed\n";
  return failures ? 1 : 0;
}
