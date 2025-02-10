#ifndef PRINTING_H_
#define PRINTING_H_

#if defined(__MINGW32__) || defined(__MINGW64__)
#define __USE_MINGW_ANSI_STDIO 1
#endif

#include <atomic>
#include <cinttypes>
#include <cstring>
#include <mutex>
#include <stdarg.h>
#include <stdio.h>

inline std::mutex &thread_printf_mutex()
{
    static std::mutex instance;
    return instance;
}

inline std::atomic<size_t> &thread_printf_counter()
{
    static std::atomic<size_t> instance;
    return instance;
}

__attribute__((format(printf, 1, 2))) inline void thread_printf(const char *fmt, ...)
{
    std::lock_guard<std::mutex> print_lk(thread_printf_mutex());
    ++thread_printf_counter();
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

template <int debug_channel>
std::atomic<bool> &enable_debugging()
{
    static std::atomic<bool> instance = {true};
    return instance;
}
#define debug_printf(...)                                            \
    do                                                               \
    {                                                                \
        if (enable_debugging<0>())                                   \
            debug_printf_impl(__FILE__, __LINE__, "\n" __VA_ARGS__); \
    } while (0)

__attribute__((format(printf, 3, 4))) inline void debug_printf_impl(const char *file_name, int line, const char *fmt, ...)
{
    std::lock_guard<std::mutex> print_lk(thread_printf_mutex());
    ++thread_printf_counter();
    if (file_name == nullptr)
    {
        file_name = "(null)";
    }
    else
    {
        ssize_t filename_last_index = strlen(file_name) - 1u;
        for (ssize_t i = filename_last_index; i >= 0; i--)
        {
            if (file_name[i] == '/')
            {
                file_name = &file_name[i + 1];
                break;
            }
        }
    }

    printf("DEBUG %s:%i", file_name, line);
    if (strlen(fmt) > 1)
    {
        ++fmt;
        printf(": ");
    }
    va_list argptr;
    va_start(argptr, fmt);
    vprintf(fmt, argptr);
    va_end(argptr);
    fflush(stdout);
}

#endif // PRINTING_H_