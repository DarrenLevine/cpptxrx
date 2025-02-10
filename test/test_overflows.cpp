#include "../examples/utils/test_utils.h"
#include "../include/cpptxrx_overflow.h"

template <class Rep, class Period>
inline constexpr void test_overflow__impl(
    const char *filename, int linenum,
    std::chrono::steady_clock::time_point lhs,
    const char *lhs_name,
    std::chrono::duration<Rep, Period> rhs,
    const char *rhs_name,
    int64_t expected)
{
    int64_t result = overflow_safe::add(lhs, rhs).time_since_epoch().count();
    thread_printf("%-10s(%20" PRId64 ") + %-10s(%20" PRId64 ")    = %" PRId64 "\n",
                  lhs_name,
                  lhs.time_since_epoch().count(),
                  rhs_name,
                  rhs.count(),
                  result);
    TEST_EQ__FL(filename, linenum, result, expected);
}
#define TEST_OVR_FLOW_ADD(lhs, rhs, expected) test_overflow__impl(__FILE__, __LINE__, lhs, #lhs, rhs, #rhs, expected)

/// @brief ""_i64 = int64_t
inline constexpr int64_t operator""_i64(unsigned long long val) noexcept { return static_cast<int64_t>(val); }

TEST_CASE(test_overflows)
{
    using sc  = std::chrono::steady_clock;
    using st  = std::chrono::steady_clock::time_point;
    using ns  = std::chrono::nanoseconds;
    using yrs = std::chrono::duration<int64_t, std::ratio<3600L * 24L * 365L>>; // years

    auto sc_now         = sc::time_point{} + ns(1421164898182); // a fake now() that's a const value for consistent testing
    const auto sc_floor = (sc_now + ns::min()).time_since_epoch().count();

    TEST_OVR_FLOW_ADD(sc_now, ns::min(), sc_floor);
    TEST_OVR_FLOW_ADD(sc_now, yrs(-10000), sc_floor);
    TEST_OVR_FLOW_ADD(sc_now, yrs(-293), sc_floor);
    TEST_OVR_FLOW_ADD(sc_now, yrs(-292), -9208510578835101818_i64);
    TEST_OVR_FLOW_ADD(sc_now, yrs(-291), -9176974578835101818_i64);
    TEST_OVR_FLOW_ADD(sc_now, ns(-1), 1421164898181_i64);
    TEST_OVR_FLOW_ADD(sc_now, ns(0), 1421164898182_i64);
    TEST_OVR_FLOW_ADD(sc_now, ns(1), 1421164898183_i64);
    TEST_OVR_FLOW_ADD(sc_now, yrs(291), 9176977421164898182_i64);
    TEST_OVR_FLOW_ADD(sc_now, yrs(292), 9208513421164898182_i64);
    TEST_OVR_FLOW_ADD(sc_now, yrs(293), ns::max().count());
    TEST_OVR_FLOW_ADD(sc_now, yrs(10000), ns::max().count());
    TEST_OVR_FLOW_ADD(sc_now, ns::max(), ns::max().count());
    thread_printf("\n");

    TEST_OVR_FLOW_ADD(st::min(), ns::min(), ns::min().count());
    TEST_OVR_FLOW_ADD(st::min(), yrs(-293), ns::min().count());
    TEST_OVR_FLOW_ADD(st::min(), yrs(-292), ns::min().count());
    TEST_OVR_FLOW_ADD(st::min(), yrs(-291), ns::min().count());
    TEST_OVR_FLOW_ADD(st::min(), ns(-1), ns::min().count());
    TEST_OVR_FLOW_ADD(st::min(), ns(0), ns::min().count());
    TEST_OVR_FLOW_ADD(st::min(), ns(1), -9223372036854775807_i64);
    TEST_OVR_FLOW_ADD(st::min(), yrs(291), -46396036854775808_i64);
    TEST_OVR_FLOW_ADD(st::min(), yrs(292), -14860036854775808_i64);
    TEST_OVR_FLOW_ADD(st::min(), yrs(293), -1_i64);
    TEST_OVR_FLOW_ADD(st::min(), ns::max(), -1_i64);
    thread_printf("\n");

    TEST_OVR_FLOW_ADD(st::max(), ns::min(), -1_i64);
    TEST_OVR_FLOW_ADD(st::max(), yrs(-293), -1_i64);
    TEST_OVR_FLOW_ADD(st::max(), yrs(-292), 14860036854775807_i64);
    TEST_OVR_FLOW_ADD(st::max(), yrs(-291), 46396036854775807_i64);
    TEST_OVR_FLOW_ADD(st::max(), ns(-1), 9223372036854775806_i64);
    TEST_OVR_FLOW_ADD(st::max(), ns(0), 9223372036854775807_i64);
    TEST_OVR_FLOW_ADD(st::max(), ns(1), 9223372036854775807_i64);
    TEST_OVR_FLOW_ADD(st::max(), yrs(291), 9223372036854775807_i64);
    TEST_OVR_FLOW_ADD(st::max(), yrs(292), 9223372036854775807_i64);
    TEST_OVR_FLOW_ADD(st::max(), yrs(293), 9223372036854775807_i64);
    TEST_OVR_FLOW_ADD(st::max(), ns::max(), 9223372036854775807_i64);
}