#ifndef CPPTXRX_OVERFLOW_H_
#define CPPTXRX_OVERFLOW_H_

#include <chrono>
#include <limits>
#include <stdint.h>

namespace overflow_safe
{
    template <typename T>
    constexpr bool would_mult_overflow(T lhs, T rhs, T &output)
    {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_mul_overflow(lhs, rhs, &output);
#else
        constexpr auto zero = T(0);
        const bool overflow =
            lhs != zero &&
            rhs != zero &&
            (lhs < zero
                 ? (rhs < zero
                        ? lhs < std::numeric_limits<T>::max() / rhs
                        : lhs < std::numeric_limits<T>::min() / rhs)
                 : (rhs < zero
                        ? rhs < std::numeric_limits<T>::min() / lhs
                        : lhs > std::numeric_limits<T>::max() / rhs));
        if (overflow)
            return true;
        output = lhs * rhs;
        return false;
#endif
    }

    template <typename T>
    constexpr bool would_mult_overflow(T lhs, T rhs)
    {
#if defined(__GNUC__) || defined(__clang__)
        T temp;
        return __builtin_mul_overflow(lhs, rhs, &temp);
#else
        constexpr auto zero = T(0);
        const bool overflow =
            lhs != zero &&
            rhs != zero &&
            (lhs < zero
                 ? (rhs < zero
                        ? lhs < std::numeric_limits<T>::max() / rhs
                        : lhs < std::numeric_limits<T>::min() / rhs)
                 : (rhs < zero
                        ? rhs < std::numeric_limits<T>::min() / lhs
                        : lhs > std::numeric_limits<T>::max() / rhs));
        return overflow;
#endif
    }
    template <typename T>
    inline constexpr bool would_add_overflow(const T lop, const T rop)
    {
        return (rop < T(0)
                    ? (lop < std::numeric_limits<T>::min() - rop)
                    : (lop > std::numeric_limits<T>::max() - rop));
    }
    template <typename T>
    constexpr bool would_add_overflow(const T lop, const T rop, T &output)
    {
        if (would_add_overflow(lop, rop))
            return false;
        output = lop + rop;
        return true;
    }

    template <class OutType, class Rep, class Period>
    constexpr OutType duration_cast(const std::chrono::duration<Rep, Period> &dur_arg)
    {
        using _CF            = std::ratio_divide<Period, typename OutType::period>;
        using OutRep         = typename OutType::rep;
        using CommonCalcType = std::common_type_t<OutRep, Rep, intmax_t>;

        if constexpr (_CF::num == 1)
        {
            if constexpr (_CF::den == 1)
                return static_cast<OutType>(static_cast<OutRep>(dur_arg.count()));
            return static_cast<OutType>(static_cast<OutRep>(static_cast<CommonCalcType>(dur_arg.count()) / static_cast<CommonCalcType>(_CF::den)));
        }

        const CommonCalcType dur = static_cast<CommonCalcType>(dur_arg.count());
        const CommonCalcType num = static_cast<CommonCalcType>(_CF::num);
        CommonCalcType dur_times_num;
        if (overflow_safe::would_mult_overflow(dur, num, dur_times_num))
        {
            if (dur_arg < std::chrono::duration<Rep, Period>{0})
                return OutType::min();
            return OutType::max();
        }
        if constexpr (_CF::den == 1)
            return OutType(static_cast<OutRep>(dur_times_num));
        return OutType(static_cast<OutRep>(dur_times_num / static_cast<CommonCalcType>(_CF::den)));
    }

    template <typename ClockType, typename Dur, class Rep, class Period>
    constexpr std::chrono::time_point<ClockType, Dur> add(
        const std::chrono::time_point<ClockType, Dur> &lhs,
        const std::chrono::duration<Rep, Period> &rhs)
    {
        const auto lhs_dur = lhs.time_since_epoch();
        const auto rhs_dur = overflow_safe::duration_cast<Dur>(rhs);
        constexpr Dur zero_dur{0};
        if (lhs_dur > zero_dur)
        {
            const auto available_positive_space_in_timepoint = Dur::max() - lhs_dur;
            if (rhs_dur > available_positive_space_in_timepoint)
                return std::chrono::time_point<ClockType, Dur>::max();
        }
        else if (lhs_dur < zero_dur)
        {
            const auto available_negative_space_in_timepoint = Dur::min() - lhs_dur;
            if (rhs_dur < available_negative_space_in_timepoint)
                return std::chrono::time_point<ClockType, Dur>::min();
        }
        return lhs + rhs_dur;
    }

    template <typename ClockType, typename Dur>
    constexpr std::chrono::time_point<ClockType, Dur> add(
        const std::chrono::time_point<ClockType, Dur> &lhs,
        const std::chrono::time_point<ClockType, Dur> &rhs)
    {
        const auto lhs_dur = lhs.time_since_epoch();
        const auto rhs_dur = rhs.time_since_epoch();
        constexpr Dur zero_dur{0};
        if (lhs_dur > zero_dur)
        {
            const auto available_positive_space_in_timepoint = Dur::max() - lhs_dur;
            if (rhs_dur > available_positive_space_in_timepoint)
                return std::chrono::time_point<ClockType, Dur>::max();
        }
        else if (lhs_dur < zero_dur)
        {
            const auto available_negative_space_in_timepoint = Dur::min() - lhs_dur;
            if (rhs_dur < available_negative_space_in_timepoint)
                return std::chrono::time_point<ClockType, Dur>::min();
        }
        return lhs + rhs_dur;
    }

    template <class ClockType = std::chrono::steady_clock, class Rep = int64_t, class Period = std::nano>
    typename ClockType::time_point now_plus(std::chrono::duration<Rep, Period> dur)
    {
        return overflow_safe::add(ClockType::now(), dur);
    }
} // namespace overflow_safe

#endif // CPPTXRX_OVERFLOW_H_