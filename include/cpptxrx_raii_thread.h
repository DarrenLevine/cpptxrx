/// @file cpptxrx_raii_thread.h
/// @author Darren V Levine (DarrenVLevine@gmail.com)
/// @brief defines a RAII thread class that is the same as std::thread but will join the thread when destructed if it is joinable
///
/// @copyright (c) 2024 Darren V Levine. This code is licensed under MIT license (see LICENSE file for details).
///
#ifndef CPPTXRX_RAII_THREAD_H_
#define CPPTXRX_RAII_THREAD_H_

#include <thread>

namespace interface
{

    /// @brief same as std::thread but will join the thread when destructed if it is joinable
    struct raii_thread : std::thread
    {
        using std::thread::detach;
        using std::thread::get_id;
        using std::thread::hardware_concurrency;
        using std::thread::id;
        using std::thread::join;
        using std::thread::joinable;
        using std::thread::native_handle;
        using std::thread::native_handle_type;
        inline raii_thread &operator=(raii_thread &&d)
        {
            std::thread::operator=(std::forward<raii_thread>(d));
            return *this;
        }
        using std::thread::swap;
        using std::thread::thread;
        inline ~raii_thread()
        {
            if (joinable())
                join();
        }
    };
} // namespace interface

#endif // CPPTXRX_RAII_THREAD_H_