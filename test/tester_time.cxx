// SPDX-License-Identifier: MIT
//
// tester_time.cxx — exercises mist::time parse / to_string.

#include <mist/time.h>

#include <cstdio>
#include <cstdlib>
#include <string>

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
    std::puts("[tester_time] round-trip");
    {
        const std::string ts = "20260608-123456";
        const auto t = mist::time::parse(ts);
        check(t.has_value(), "parse: valid timestamp");
        if (t)
            check(mist::time::to_string(*t) == ts, "round-trip parse -> to_string");
    }

    std::puts("[tester_time] ordering");
    {
        const auto a = mist::time::parse("20260608-120000");
        const auto b = mist::time::parse("20260608-120001");
        check(a && b, "parse: both valid");
        if (a && b)
            check(*b > *a, "one second later compares greater");
    }
    {
        const auto day1 = mist::time::parse("20260101-000000");
        const auto day2 = mist::time::parse("20260102-000000");
        check(day1 && day2 && (*day2 - *day1 == 86400),
              "consecutive days differ by 86400 s");
    }

    std::puts("[tester_time] invalid input");
    {
        check(!mist::time::parse("not-a-timestamp").has_value(), "garbage -> nullopt");
        check(!mist::time::parse("").has_value(), "empty -> nullopt");
        check(!mist::time::parse("2026-06-08 12:00").has_value(), "wrong format -> nullopt");
    }

    if (failures)
    {
        std::printf("[tester_time] %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("[tester_time] OK");
    return EXIT_SUCCESS;
}
