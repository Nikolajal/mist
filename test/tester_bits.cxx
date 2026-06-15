// SPDX-License-Identifier: MIT
//
// tester_bits.cxx — exercises mist::bits encode/decode helpers.

#include <mist/bits.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace b = mist::bits;

namespace
{

int failures = 0;

void check(bool cond, const char *what)
{
    if (!cond)
    {
        std::printf("  FAIL: %s\n", what);
        ++failures;
    }
}

} // namespace

int main()
{
    std::puts("[tester_bits] encode_bit");
    check(b::encode_bit(0) == 0x1u, "bit 0");
    check(b::encode_bit(1) == 0x2u, "bit 1");
    check(b::encode_bit(31) == 0x80000000u, "bit 31");
    static_assert(b::encode_bit(4) == 0x10u, "encode_bit is constexpr");
#ifdef NDEBUG
    // Out-of-range is a debug-build assert; release silently returns 0 / skips.
    check(b::encode_bit(32) == 0u, "out-of-range -> 0 (release path)");
#endif

    std::puts("[tester_bits] encode_bits");
    check(b::encode_bits(std::vector<std::uint8_t>{0, 1, 2}) == 0x7u, "0,1,2 -> 0b111");
    check(b::encode_bits(std::vector<int>{3, 5}) == 0x28u, "int range 3,5");
    check(b::encode_bits(std::array<int, 0>{}) == 0u, "empty -> 0");
#ifdef NDEBUG
    check(b::encode_bits(std::vector<int>{2, 40, 3}) == 0xCu, "out-of-range skipped (release)");
#endif
    {
        constexpr std::array<int, 3> idx{0, 4, 8};
        static_assert(b::encode_bits(idx) == 0x111u, "encode_bits is constexpr");
    }

    std::puts("[tester_bits] count_trailing_zeros");
    check(b::count_trailing_zeros(0u) == 32, "zero -> 32");
    check(b::count_trailing_zeros(0x1u) == 0, "0x1 -> 0");
    check(b::count_trailing_zeros(0x8u) == 3, "0x8 -> 3");
    check(b::count_trailing_zeros(0x80000000u) == 31, "msb -> 31");
    static_assert(b::count_trailing_zeros(0x10u) == 4, "ctz is constexpr");

    std::puts("[tester_bits] decode_bits");
    {
        const auto v = b::decode_bits(0u);
        check(v.empty(), "decode 0 -> empty");
    }
    {
        const auto v = b::decode_bits(0x7u);
        check((v == std::vector<std::uint8_t>{0, 1, 2}), "decode 0b111 -> {0,1,2}");
    }
    {
        const auto v = b::decode_bits(0x80000001u);
        check((v == std::vector<std::uint8_t>{0, 31}), "decode -> {0,31}");
    }

    // Round-trip: encode_bits(decode_bits(m)) == m for arbitrary masks.
    std::puts("[tester_bits] round-trip");
    for (std::uint32_t m : {0u, 0x1u, 0xCAFEu, 0xFFFFFFFFu, 0x80000000u})
    {
        check(b::encode_bits(b::decode_bits(m)) == m, "encode_bits(decode_bits(m)) == m");
    }

    if (failures)
    {
        std::printf("[tester_bits] %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("[tester_bits] OK");
    return EXIT_SUCCESS;
}
