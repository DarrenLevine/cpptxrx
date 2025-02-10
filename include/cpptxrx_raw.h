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
    /// @tparam   timeouts<...> (optional): default recv, send, open, and close timeouts in ns,
    ///               only used when no timeout is specfied on the operation.
    template <typename open_opts_type, typename default_timeouts_type = timeouts<>>
    using raw = raw_factory<open_opts_type, default_timeouts_type>;
} // namespace interface

#include "cpptxrx_short_form.h"

#endif // CPPTXRX_RAW_H_