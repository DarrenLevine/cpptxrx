/// @brief includes copies all the examples into one file that can be run all at once
/// note that this is mainly a test to ensure that all the examples compile, since the
/// examples largely don't have testing asserts.

// pre-load headers so that they don't get pushed into the test namespace
#include "../examples/utils/test_utils.h"
#include "../include/cpptxrx_raw.h"
#include "../include/cpptxrx_threadsafe.h"
#include "../include/default_tcp.h"
#include "../include/default_udp.h"
#include <arpa/inet.h>
#include <cstring>
#include <signal.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

// using_a_threadsafe_interface
#define main using_a_threadsafe_interface
namespace test01
{
    int using_a_threadsafe_interface();
#include "../examples/01_using_a_threadsafe_interface.cpp"
} // namespace test01
TEST_CASE(using_a_threadsafe_interface_cpp)
{
    const int ret = test01::using_a_threadsafe_interface();
    TEST_EQ(ret, 0);
}
#undef main
// using_a_raw_interface
#define main using_a_raw_interface
namespace test02
{
    int using_a_raw_interface();
#include "../examples/02_using_a_raw_interface.cpp"
} // namespace test02
TEST_CASE(using_a_raw_interface_cpp)
{
    const int ret = test02::using_a_raw_interface();
    TEST_EQ(ret, 0);
}
#undef main

// receive_callbacks
#define main receive_callbacks
namespace test03
{
    int receive_callbacks();
#include "../examples/03_receive_callbacks.cpp"
} // namespace test03
TEST_CASE(receive_callbacks_cpp)
{
    const int ret = test03::receive_callbacks();
    TEST_EQ(ret, 0);
}
#undef main

// chaining_filters
#define main chaining_filters
namespace test04
{
    int chaining_filters();
#include "../examples/04_chaining_filters.cpp"
} // namespace test04
TEST_CASE(chaining_filters_cpp)
{
    const int ret = test04::chaining_filters();
    TEST_EQ(ret, 0);
}
#undef main

// using_runtime_polymorphism
#define main using_runtime_polymorphism
namespace test05
{
    int using_runtime_polymorphism();
#include "../examples/05_using_runtime_polymorphism.cpp"
} // namespace test05
TEST_CASE(sing_runtime_polymorphism_cpp)
{
    const int ret = test05::using_runtime_polymorphism();
    TEST_EQ(ret, 0);
}
#undef main

// using_comptime_polymorphism
#define main using_comptime_polymorphism
namespace test06
{
    int using_comptime_polymorphism();
#include "../examples/06_using_comptime_polymorphism.cpp"
} // namespace test06
TEST_CASE(using_comptime_polymorphism_cpp)
{
    const int ret = test06::using_comptime_polymorphism();
    TEST_EQ(ret, 0);
}
#undef main

// wrapping_a_udp_socket
#define main wrapping_a_udp_socket
namespace test07
{
    int wrapping_a_udp_socket();
#include "../examples/07_wrapping_a_udp_socket.cpp"
} // namespace test07
TEST_CASE(wrapping_a_udp_socket_cpp)
{
    const int ret = test07::wrapping_a_udp_socket();
    TEST_EQ(ret, 0);
}
#undef main

namespace test08
{
#include "../examples/08_using_a_tcp_interface.cpp"
} // namespace test08
