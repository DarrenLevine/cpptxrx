/// @file cpptxrx_raw.h
/// @author Darren V Levine (DarrenVLevine@gmail.com)
/// @brief defines the "interface::raw" class template type, for creating non-thread-safe interfaces in the CppTxRx style
///
/// @copyright (c) 2024 Darren V Levine. This code is licensed under MIT license (see LICENSE file for details).
///
#ifndef CPPTXRX_RAW_H_
#define CPPTXRX_RAW_H_

// the following macro manipulation imports (or re-imports) "cpptxrx_factory.h" but with the threadsafe features/overhead turned off
#define CPPTXRX_THREADSAFE 0
#ifdef CPPTXRX_FACTORY_H_
#undef CPPTXRX_FACTORY_H_
#endif
#include "cpptxrx_factory.h"
#undef CPPTXRX_THREADSAFE

namespace interface
{
    /// @brief a inheritable base class for creating non-thread safe CppTxRx interfaces
    ///
    /// @tparam   open_opts_type: options type to use when calling "open"
    /// @tparam   default_recv_timeout_ns (optional): default recv timeout in ns
    /// @tparam   default_send_timeout_ns (optional): default send timeout in ns
    /// @tparam   default_open_timeout_ns (optional): default open timeout in ns
    /// @tparam   default_clse_timeout_ns (optional): default close timeout in ns
    template <typename open_opts_type,
              uint64_t default_recv_timeout_ns = std::chrono::nanoseconds(std::chrono::seconds(30)).count(),
              uint64_t default_send_timeout_ns = std::chrono::nanoseconds(std::chrono::seconds(1)).count(),
              uint64_t default_open_timeout_ns = std::chrono::nanoseconds(std::chrono::seconds(1)).count(),
              uint64_t default_clse_timeout_ns = std::chrono::nanoseconds(std::chrono::seconds(1)).count()>
    using raw = raw_factory<open_opts_type,
                            default_recv_timeout_ns,
                            default_send_timeout_ns,
                            default_open_timeout_ns,
                            default_clse_timeout_ns>;
} // namespace interface

#endif // CPPTXRX_RAW_H_