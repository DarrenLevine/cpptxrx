#ifndef TESTING_H_
#define TESTING_H_

#include "printing.h"
#include <atomic>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdint.h>
#include <vector>

std::atomic<size_t> test_count = {0u};

struct test_details
{
    void (*test_func)();
    const char *test_name;
    const char *filename;
    int linenum;
};
std::vector<test_details> test_list;
class test_failure : public std::exception
{
public:
    virtual const char *what() const throw()
    {
        return "test failed";
    }
};
#define CPPTXRX_FORMATTER_MAPPING(t, f)             \
    inline void test_value_printer(t value)         \
    {                                               \
        thread_printf("(" #t "): %" f "\n", value); \
    }                                               \
    static_assert(1)

CPPTXRX_FORMATTER_MAPPING(int, "i");
CPPTXRX_FORMATTER_MAPPING(long int, "li");
CPPTXRX_FORMATTER_MAPPING(long long int, "lli");
CPPTXRX_FORMATTER_MAPPING(unsigned int, "u");
CPPTXRX_FORMATTER_MAPPING(unsigned long int, "lu");
CPPTXRX_FORMATTER_MAPPING(unsigned long long int, "llu");
CPPTXRX_FORMATTER_MAPPING(const char *, "s");
CPPTXRX_FORMATTER_MAPPING(char, "c");
CPPTXRX_FORMATTER_MAPPING(bool, "i");
CPPTXRX_FORMATTER_MAPPING(double, "f");

#undef CPPTXRX_FORMATTER_MAPPING

inline void test_value_printer(float value)
{
    thread_printf("(float): %f\n", static_cast<double>(value));
}

template <typename T>
void test_value_printer(const std::vector<T> &value)
{
    std::lock_guard<std::mutex> print_lk(thread_printf_mutex());
    ++thread_printf_counter();
    if (value.size() == 0)
    {
        printf("std::vector<>{}\n");
    }
    else
    {
        printf("std::vector<>{");
        auto last_index = value.size() - 1u;
        for (size_t i = 0; i < last_index; i++)
            std::cout << value[i] << ", ";
        std::cout << value[last_index] << "}\n";
    }
    fflush(stdout);
}
template <size_t array_length>
void test_value_printer(const uint8_t (&bytes)[array_length])
{
    char output[array_length * 3] = "";
    for (size_t i = 0, j = 0; i < array_length; i++, j += 3)
    {
        uint8_t bottom = bytes[i] % 16;
        uint8_t top    = bytes[i] / 16;
        output[j]      = (top > 9 ? 'A' - 10 : '0') + top;
        output[j + 1]  = (bottom > 9 ? 'A' - 10 : '0') + bottom;
        if (i + 1u < array_length)
            output[j + 2] = ':';
        else
            output[j + 2] = 0;
    }
    thread_printf("(uint8_t[%zu]): %s \n", array_length, output);
}

template <typename TComp, typename T1, typename T2>
void test_eval(const char *test_func_name, const char *filename, int linenum, T1 &&x, T2 &&y)
{
    ++test_count;
    if (!TComp()(x, y))
    {
        size_t max_pad       = 0;
        size_t test_name_len = strlen(test_func_name);
        size_t pad_size      = test_name_len < max_pad ? max_pad - test_name_len : 0u;
        thread_printf(" -> [\x1B[31mFAIL\x1B[0m] %s(A, B) %-*s @ %s:%i\n",
                      test_func_name, static_cast<int>(pad_size), "", filename, linenum);

        thread_printf("    A ");
        test_value_printer(x);
        thread_printf("    B ");
        test_value_printer(y);
        throw test_failure();
    }
}

template <typename TComp, typename T1, typename T2>
__attribute__((format(printf, 6, 7))) void test_eval(const char *test_func_name, const char *filename, int linenum, T1 &&x, T2 &&y, const char *format, ...)
{
    ++test_count;
    if (!TComp()(x, y))
    {
        {
            std::lock_guard<std::mutex> print_lk(thread_printf_mutex());
            ++thread_printf_counter();
            printf(" -> [\x1B[31mFAIL\x1B[0m] %s(A, B) - \"", test_func_name);
            va_list argptr;
            va_start(argptr, format);
            vprintf(format, argptr);
            va_end(argptr);
            printf("\" @ %s:%i\n", filename, linenum);
            fflush(stdout);
        }
        thread_printf("    A ");
        test_value_printer(x);
        thread_printf("    B ");
        test_value_printer(y);
        throw test_failure();
    }
}
struct test_registration
{
    inline test_registration(test_details &&test_data)
    {
        test_list.emplace_back(std::forward<test_details &&>(test_data));
    }
};

#define CAT_NAMES(A, B) A##B
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsuggest-attribute=noreturn"
#define TEST_CASE(name)                                                                \
    void name();                                                                       \
    test_registration CAT_NAMES(registered_, name)({name, #name, __FILE__, __LINE__}); \
    void name()

struct equals_compare
{
    template <typename T1, typename T2>
    bool operator()(T1 &&x, T2 &&y)
    {
        return x == y;
    }
};
#define TEST_EQ(a, ...) test_eval<equals_compare>("TEST_EQ", __FILE__, __LINE__, a, __VA_ARGS__)
#define TEST_EQ__FL(f, l, a, ...) test_eval<equals_compare>("TEST_EQ", f, l, a, __VA_ARGS__)
struct greater_compare
{
    template <typename T1, typename T2>
    bool operator()(T1 &&x, T2 &&y)
    {
        return x > y;
    }
};
#define TEST_GT(a, ...) test_eval<greater_compare>("TEST_GT", __FILE__, __LINE__, a, __VA_ARGS__)
#define TEST_GT__FL(f, l, a, ...) test_eval<greater_compare>("TEST_GT", f, l, a, __VA_ARGS__)
struct greater_equal_compare
{
    template <typename T1, typename T2>
    bool operator()(T1 &&x, T2 &&y)
    {
        return x >= y;
    }
};
#define TEST_GTE(a, ...) test_eval<greater_equal_compare>("TEST_GTE", __FILE__, __LINE__, a, __VA_ARGS__)
#define TEST_GTE__FL(f, l, a, ...) test_eval<greater_equal_compare>("TEST_GTE", f, l, a, __VA_ARGS__)
struct less_compare
{
    template <typename T1, typename T2>
    bool operator()(T1 &&x, T2 &&y)
    {
        return x < y;
    }
};
#define TEST_LT(a, ...) test_eval<less_compare>("TEST_LT", __FILE__, __LINE__, a, __VA_ARGS__)
#define TEST_LT__FL(f, l, a, ...) test_eval<less_compare>("TEST_LT", f, l, a, __VA_ARGS__)
struct less_equal_compare
{
    template <typename T1, typename T2>
    bool operator()(T1 &&x, T2 &&y)
    {
        return x <= y;
    }
};
#define TEST_LTE(a, ...) test_eval<less_equal_compare>("TEST_LTE", __FILE__, __LINE__, a, __VA_ARGS__)
#define TEST_LTE__FL(f, l, a, ...) test_eval<less_equal_compare>("TEST_LTE", f, l, a, __VA_ARGS__)
struct mem_compare
{
    template <typename T1, typename T2>
    bool operator()(T1 &&x, T2 &&y)
    {
        static_assert(sizeof(T1) == sizeof(T2), "can't mem compare two differently sized types");
        return std::memcmp(&x, &y, sizeof(T1)) == 0;
    }
};
#define TEST_MEM_CPR(a, ...) test_eval<mem_compare>("TEST_MEM_CPR", __FILE__, __LINE__, a, __VA_ARGS__)
struct cstr_compare
{
    inline bool operator()(const char *x, const char *y)
    {
        if (x == y)
            return true;
        if (x == nullptr || y == nullptr)
            return false;
        return strcmp(x, y) == 0;
    }
};
#define TEST_CSTR_CPR(a, ...) test_eval<cstr_compare>("TEST_CSTR_CPR", __FILE__, __LINE__, a, __VA_ARGS__)
#define TEST_CSTR_CPR__FL(f, l, a, ...) test_eval<cstr_compare>("TEST_CSTR_CPR", f, l, a, __VA_ARGS__)

std::atomic<size_t> test_case_finished_count = 0;
std::atomic<size_t> test_case_total_count    = 0;

#define IN_THREAD(...)                                                                                                                          \
    do                                                                                                                                          \
    {                                                                                                                                           \
        bool failure_occurred = true;                                                                                                           \
        try                                                                                                                                     \
        {                                                                                                                                       \
            __VA_ARGS__;                                                                                                                        \
            failure_occurred = false;                                                                                                           \
        }                                                                                                                                       \
        catch (test_failure &)                                                                                                                  \
        {                                                                                                                                       \
        }                                                                                                                                       \
        catch (std::exception & e)                                                                                                              \
        {                                                                                                                                       \
            thread_printf("UNKNOWN FAILURE: %s\n", e.what());                                                                                   \
        }                                                                                                                                       \
        catch (...)                                                                                                                             \
        {                                                                                                                                       \
            thread_printf("UNKNOWN FAILURE\n");                                                                                                 \
        }                                                                                                                                       \
        if (failure_occurred)                                                                                                                   \
        {                                                                                                                                       \
            thread_printf("--------------------\n");                                                                                            \
            thread_printf("\x1B[31mFAILED\x1B[0m %zu/%zu test cases passed.\n", test_case_finished_count.load(), test_case_total_count.load()); \
            exit(EXIT_FAILURE);                                                                                                                 \
        }                                                                                                                                       \
    } while (0)

int main()
{
    test_case_total_count = test_list.size();
    try
    {
        size_t test_case_count = 0;
        for (auto &test : test_list)
        {
            ++test_case_count;
            size_t test_name_len = strlen(test.test_name);
            size_t max_pad       = 0;
            size_t pad_size      = test_name_len < max_pad ? max_pad - test_name_len : 0u;
            thread_printf("[\x1B[32mSTARTED\x1B[0m] TEST_CASE %zu/%zu (%s)%-*s @ %s:%i\n",
                          test_case_count, test_list.size(), test.test_name,
                          static_cast<int>(pad_size), "", test.filename, test.linenum);
            size_t pre_test_prints = thread_printf_counter();
            size_t pre_test_count  = test_count;
            test.test_func();
            if (pre_test_prints == thread_printf_counter())
                thread_printf("\033[A\33[2KT\r"); // remove the previous line if the test itself didn't print
            size_t tests_ran = test_count - pre_test_count;
            if (tests_ran == 0u)
            {
                thread_printf("[\x1B[33mNO TESTS\x1B[0m] TEST_CASE %zu/%zu (%s)%-*s @ %s:%i\n",
                              test_case_count, test_list.size(), test.test_name,
                              static_cast<int>(pad_size), "", test.filename, test.linenum);
                throw test_failure();
            }
            else
            {
                thread_printf("[\x1B[32mGOOD - passed %zu tests\x1B[0m] TEST_CASE %zu/%zu (%s)%-*s @ %s:%i\n",
                              tests_ran, test_case_count, test_list.size(), test.test_name,
                              static_cast<int>(pad_size), "", test.filename, test.linenum);
                ++test_case_finished_count;
            }
        }
    }
    catch (test_failure &)
    {
    }
    catch (std::exception &e)
    {
        thread_printf("UNKNOWN FAILURE: %s\n", e.what());
    }
    catch (...)
    {
        thread_printf("UNKNOWN FAILURE\n");
    }
    thread_printf("--------------------\n");
    if (test_count == 0)
    {
        thread_printf("\x1B[33mNO TESTS\x1B[0m %zu/%zu test cases passed.\n", test_case_finished_count.load(), test_case_total_count.load());
        return -2;
    }

    if (test_case_finished_count < test_case_total_count)
    {
        thread_printf("\x1B[31mFAILED\x1B[0m %zu/%zu test cases passed.\n", test_case_finished_count.load(), test_case_total_count.load());
        return -1;
    }

    thread_printf("\x1B[32mPASSED\x1B[0m %zu/%zu test cases passed.\n", test_case_finished_count.load(), test_case_total_count.load());
    return 0;
}

#endif // TESTING_H_

// Examples:
//   TEST_CASE(test_something1)
//   {
//       int abc = 1;
//       TEST_EQ(1, abc, "testing this thing");
//       TEST_EQ(2, 2);
//       TEST_EQ(2, 2, "message");
//       TEST_EQ(6, 6);
//   }
//   TEST_CASE(test_something2)
//   {
//       TEST_EQ(1, 1);
//       thread_printf("in test print\n");
//       TEST_EQ(2, 2);
//       TEST_EQ(6, 6);
//       TEST_EQ(6, 6);
//       TEST_EQ(6, 6);
//       TEST_EQ(6, 6);
//       TEST_EQ(6, 6);
//       TEST_EQ(6, 6);
//   }
//   TEST_CASE(test_something3)
//   {
//       TEST_EQ(1, 1);
//       TEST_EQ(2, 2);
//       // TEST_EQ(6, 5);
//       uint8_t data1[10] = {0, 2, 3};
//       uint8_t data2[10] = {0, 2, 3};
//       TEST_EQ(data1, data2);
//       TEST_EQ("bob", "larry");
//   }