#ifndef NIL_COMPILER_BINARY32_HPP
#define NIL_COMPILER_BINARY32_HPP

#include <cstdint>
#include <cstring>
#include <limits>

namespace ny {

// Language f32 values use IEEE binary32 storage and a binary64 execution
// carrier containing its exact widened value. These conversions use integer
// operations only: rounding is nearest, ties to even, regardless of the host
// floating-point rounding mode or denormal controls. Overflow produces signed
// infinity; underflow preserves the sign of zero. NaNs become one quiet NaN;
// payload/sign preservation and a foreign C float ABI are not promised here.
static_assert(sizeof(double) == sizeof(std::uint64_t) &&
              std::numeric_limits<double>::is_iec559 &&
              std::numeric_limits<double>::digits == 53 &&
              std::numeric_limits<double>::max_exponent == 1024,
              "NIL floating-point carriers require IEEE binary64");

namespace binary32_detail {

inline unsigned highest_bit(std::uint64_t value) noexcept {
  unsigned result = 0;
  while (value >>= 1) ++result;
  return result;
}

inline std::uint64_t rounded_right(std::uint64_t value, unsigned shift) noexcept {
  if (shift == 0) return value;
  if (shift > 64) return 0;
  if (shift == 64) return value > (std::uint64_t{1} << 63) ? 1 : 0;
  const auto truncated = value >> shift;
  const auto remainder = value & ((std::uint64_t{1} << shift) - 1);
  const auto halfway = std::uint64_t{1} << (shift - 1);
  return truncated + (remainder > halfway ||
                      (remainder == halfway && (truncated & 1)));
}

// Encode the exact nonnegative value significand * 2^exponent. The source
// significand fits uint64_t; exponent is supplied only by the adapters below.
inline std::uint32_t pack(std::uint32_t sign, std::uint64_t significand,
                          int exponent) noexcept {
  if (!significand) return sign;
  const auto top = highest_bit(significand);
  int unbiased = static_cast<int>(top) + exponent;
  if (unbiased > 127) return sign | 0x7f800000U;
  if (unbiased >= -126) {
    auto rounded = top > 23 ? rounded_right(significand, top - 23)
                            : significand << (23 - top);
    if (rounded == (std::uint64_t{1} << 24)) {
      rounded >>= 1;
      if (++unbiased > 127) return sign | 0x7f800000U;
    }
    return sign | (static_cast<std::uint32_t>(unbiased + 127) << 23) |
           (static_cast<std::uint32_t>(rounded) & 0x007fffffU);
  }
  // Subnormals are integer multiples of 2^-149. A rounded 0x800000 is
  // already the encoding of the smallest normal value.
  const auto scale = exponent + 149;
  const auto rounded = scale >= 0 ? significand << static_cast<unsigned>(scale)
      : rounded_right(significand, static_cast<unsigned>(-scale));
  return sign | static_cast<std::uint32_t>(rounded);
}

inline std::uint64_t carrier_bits(double value) noexcept {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

inline double carrier_from_bits(std::uint64_t bits) noexcept {
  double value = 0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

} // namespace binary32_detail

inline std::uint32_t binary32_from_f64(double value) noexcept {
  const auto bits = binary32_detail::carrier_bits(value);
  const auto sign = static_cast<std::uint32_t>(bits >> 32) & 0x80000000U;
  const auto exponent = static_cast<unsigned>((bits >> 52) & 0x7ff);
  const auto fraction = bits & 0x000fffffffffffffULL;
  if (exponent == 0x7ff)
    return fraction ? 0x7fc00000U : sign | 0x7f800000U;
  return binary32_detail::pack(sign,
      exponent ? fraction | (std::uint64_t{1} << 52) : fraction,
      exponent ? static_cast<int>(exponent) - 1023 - 52 : -1074);
}

inline std::uint32_t binary32_from_u64(std::uint64_t value) noexcept {
  return binary32_detail::pack(0, value, 0);
}

inline std::uint32_t binary32_from_i64(std::int64_t value) noexcept {
  // Form the magnitude in unsigned arithmetic, including INT64_MIN. Going
  // through double first would double-round integers near binary32 midpoints.
  const auto magnitude = value < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(value)
                                  : static_cast<std::uint64_t>(value);
  return binary32_detail::pack(value < 0 ? 0x80000000U : 0, magnitude, 0);
}

inline double binary32_to_f64(std::uint32_t bits) noexcept {
  const auto sign = static_cast<std::uint64_t>(bits & 0x80000000U) << 32;
  const auto exponent = (bits >> 23) & 0xff;
  const auto fraction = bits & 0x007fffffU;
  if (exponent == 0xff)
    return binary32_detail::carrier_from_bits(fraction ? 0x7ff8000000000000ULL
                                                      : sign | 0x7ff0000000000000ULL);
  if (exponent)
    return binary32_detail::carrier_from_bits(sign |
        (static_cast<std::uint64_t>(exponent + (1023 - 127)) << 52) |
        (static_cast<std::uint64_t>(fraction) << 29));
  if (!fraction) return binary32_detail::carrier_from_bits(sign);
  const auto top = binary32_detail::highest_bit(fraction);
  return binary32_detail::carrier_from_bits(sign |
      (static_cast<std::uint64_t>(static_cast<int>(top) - 149 + 1023) << 52) |
      ((static_cast<std::uint64_t>(fraction) << (52 - top)) & 0x000fffffffffffffULL));
}

inline double binary32_round(double value) noexcept {
  return binary32_to_f64(binary32_from_f64(value));
}

inline double binary32_round_i64(std::int64_t value) noexcept {
  return binary32_to_f64(binary32_from_i64(value));
}

inline bool binary32_is_canonical(double value) noexcept {
  return binary32_detail::carrier_bits(value) ==
         binary32_detail::carrier_bits(binary32_round(value));
}

} // namespace ny
#endif
