#ifndef PRINTING_H_
#define PRINTING_H_

#include <atomic>
#include <mutex>
#include <stdarg.h>
#include <stdio.h>

template <int = 0>
std::mutex &thread_printf_mutex()
{
    static std::mutex instance;
    return instance;
}
__attribute__((format(printf, 1, 2))) inline void thread_printf(const char *format, ...)
{
    std::lock_guard<std::mutex> print_lk(thread_printf_mutex());
    va_list argptr;
    va_start(argptr, format);
    vprintf(format, argptr);
    va_end(argptr);
    fflush(stdout);
}

template <int debug_channel>
std::atomic<bool> &enable_debugging()
{
    static std::atomic<bool> instance = {true};
    return instance;
}
#define debug_printf(...)                                       \
    do                                                          \
    {                                                           \
        if (enable_debugging<0>())                              \
            debug_printf_impl(__FILE__, __LINE__, __VA_ARGS__); \
    } while (0)
__attribute__((format(printf, 3, 4))) static void debug_printf_impl(const char *file_name, int line, const char *format, ...)
{
    std::lock_guard<std::mutex> print_lk(thread_printf_mutex());
    printf("DEBUG %s:%i: ", file_name, line);
    va_list argptr;
    va_start(argptr, format);
    vprintf(format, argptr);
    va_end(argptr);
    fflush(stdout);
}

#endif // PRINTING_H_