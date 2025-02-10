/// @brief tests "interface::filters" classes, and "interface::filter" data processing capabilities
#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

struct filter_trace_t
{
    const char *name;
    const void *input;
    const void *output;
    const char *status;
};
std::mutex recorded_filter_data_mutex;
std::vector<filter_trace_t> recorded_filter_data;

#define CPPTXRX_TRACE_FILTER_PRE_PROCESS()                      \
    std::lock_guard<std::mutex> lk(recorded_filter_data_mutex); \
    filter_trace_t trace_item { name, input.data, nullptr, nullptr }
#define CPPTXRX_TRACE_FILTER_POST_PROCESS()         \
    do                                              \
    {                                               \
        trace_item.output = output.data;            \
        trace_item.status = r.c_str();              \
        recorded_filter_data.push_back(trace_item); \
    } while (0)

// testing and printing utils
#include "../examples/utils/test_utils.h"
// threadsafe is imported after, in case thread_printf is needed during debugging
#include "../include/cpptxrx_threadsafe.h"

static void change_nulls_to_displayable_chars(char *data, ssize_t size)
{
    for (ssize_t i = 0; i < size; i++)
        if (data[i] == 0)
            data[i] = '$';
    data[size] = 0;
}

static constexpr size_t max_packet_size = 1000;

static void display_send_data(const uint8_t *data, size_t size, const char *tag)
{
    char display_data[max_packet_size] = {};
    memcpy(display_data, data, size);
    change_nulls_to_displayable_chars(display_data, size);
    thread_printf("\t%s sending \"%s\"\n", tag, display_data);
}
static void display_receive_data(const uint8_t *data, size_t size, const char *tag)
{
    char display_data[max_packet_size] = {};
    memcpy(display_data, data, size);
    change_nulls_to_displayable_chars(display_data, size);
    thread_printf("\t%s receiving \"%s\" (%zu bytes)\n", tag, display_data, size);
}

std::vector<std::vector<uint8_t>> recorded_tx_data;

std::vector<uint8_t> desired_rx_data;
std::mutex desired_rx_data_mutex;

namespace demo
{
    struct passthrough : interface::thread_safe<interface::no_opts>
    {
    public:
        CPPTXRX_IMPORT_CTOR_AND_DTOR(passthrough);

        [[nodiscard]] const char *name() const final
        {
            return "demo::passthrough";
        }

    private:
        void construct() final {}
        void destruct() final {}
        void process_close() final
        {
            transactions.p_close_op->end_op();
        }
        void process_open() final
        {
            transactions.p_open_op->end_op();
        }
        void process_send_receive() final
        {
            if (transactions.p_send_op)
            {
                recorded_tx_data.push_back(
                    std::vector<uint8_t>(transactions.p_send_op->send_data,
                                         transactions.p_send_op->send_data + transactions.p_send_op->send_size));
                display_send_data(transactions.p_send_op->send_data, transactions.p_send_op->send_size, "final");
                transactions.p_send_op->end_op();
            }
            if (transactions.p_recv_op)
            {
                char rx_data[max_packet_size] = "";
                size_t rx_size                = 0;
                {
                    std::lock_guard<std::mutex> lk(desired_rx_data_mutex);
                    for (auto rx_char : desired_rx_data)
                        rx_data[rx_size++] = rx_char;
                }
                if (rx_size > transactions.p_recv_op->max_receive_size)
                {
                    thread_printf("\tERROR: line NOT received \"%s\"\n", rx_data);
                    transactions.p_recv_op->end_op_with_error_code(123, "RX_SIZE_LARGER_THAN_MAX");
                    return;
                }
                memcpy(transactions.p_recv_op->received_data, rx_data, rx_size);
                transactions.p_recv_op->received_size = rx_size;
                transactions.p_recv_op->end_op();
                thread_printf("\tline receive \"%s\"\n", rx_data);
            }
        }
        void wake_process() final {} // none of the process_ methods can block, so no need to do anything here
    };
} // namespace demo

using pointer_indexes  = std::vector<int>;
using expected_outputs = std::vector<const char *>;

inline void check_pointers(const char *filename, int linenum, std::vector<int> &expected_mem_counter, const uint8_t *data, size_t i, const char *description, const bool is_receiving)
{
    // checking to see if the filter traces passed the expected memory pointers, only if expected_mem_counter is specified
    std::lock_guard<std::mutex> lk(recorded_filter_data_mutex);
    if (expected_mem_counter.size() > 0u)
    {
        const void *ptr = static_cast<const void *>(data);
        std::vector<const void *> observed_ptrs;
        if (!is_receiving)
            observed_ptrs.push_back(ptr);
        size_t mem_index = 0;

        std::vector<int> observed_mem_counter;
        for (auto &item : recorded_filter_data)
        {
            if (ptr != item.input)
            {
                ptr       = item.input;
                mem_index = std::find(observed_ptrs.begin(), observed_ptrs.end(), ptr) - observed_ptrs.begin();
                if (mem_index >= observed_ptrs.size())
                    observed_ptrs.push_back(ptr);
            }
            observed_mem_counter.push_back(mem_index);
            if (ptr != item.output)
            {
                ptr       = item.output;
                mem_index = std::find(observed_ptrs.begin(), observed_ptrs.end(), ptr) - observed_ptrs.begin();
                if (mem_index >= observed_ptrs.size())
                    observed_ptrs.push_back(ptr);
            }
            observed_mem_counter.push_back(mem_index);
        }

        bool receive_end_okay = true;
        if (is_receiving)
        {
            if (ptr != static_cast<const void *>(data))
            {
                ptr       = static_cast<const void *>(data);
                mem_index = std::find(observed_ptrs.begin(), observed_ptrs.end(), ptr) - observed_ptrs.begin();
                if (mem_index >= observed_ptrs.size())
                {
                    observed_ptrs.push_back(ptr);
                    observed_mem_counter.push_back(mem_index);
                    receive_end_okay = false;
                }
            }
        }

        if (observed_mem_counter != expected_mem_counter || !receive_end_okay)
        {
            if (!is_receiving)
                debug_printf("send data->%p\n", static_cast<const void *>(data));
            for (auto &item : recorded_filter_data)
                debug_printf("%s %p->%p : %s\n", item.name, item.input, item.output, item.status);
            if (is_receiving)
                debug_printf("receive data->%p\n", static_cast<const void *>(data));
            const char *rx_okay = receive_end_okay ? "" : " Final pointer was not receive output!";
            TEST_EQ__FL(filename, linenum,
                        observed_mem_counter, expected_mem_counter,
                        "send(%s).chain: [%s] repeated_run=%zu. Failed to chain pointers in expected order.%s",
                        data, description, i, rx_okay);
            TEST_EQ__FL(filename, linenum,
                        receive_end_okay, true,
                        "send(%s).chain: [%s] repeated_run=%zu. Failed to chain pointers in expected order.%s",
                        data, description, i, rx_okay);
        }
    }
    recorded_filter_data.clear();
}

template <size_t N, typename T>
void test_send_filter_impl(const char *filename, int linenum, const char (&in_data)[N], T test_filter, const char *description,
                           std::vector<const char *> &&expected, std::vector<int> &&expected_mem_counter = {}, size_t test_times = 3)
{
    demo::passthrough server(
        interface::common_opts()
            .send_filter(&test_filter));
    for (size_t i = 0; i < test_times; ++i)
    {
        uint8_t data[N];
        memcpy(data, in_data, N);
        thread_printf("\n>>> send(%s).chain: [%s]\n\n", data, description);
        auto tx_status = server.send(data, sizeof(data) - 1);

        TEST_EQ__FL(filename, linenum, static_cast<int>(tx_status), static_cast<int>(interface::status_e::SUCCESS),
                    "send(%s).chain: [%s], send status=%s (%s)\n", data, description,
                    tx_status.c_str(), tx_status.additional_error_info());

        TEST_EQ__FL(filename, linenum, expected.size(), recorded_tx_data.size(), "send(%s).chain: [%s]", data, description);
        for (size_t ii = 0; ii < expected.size(); ii++)
        {
            recorded_tx_data[ii].push_back(0); // add a null char for the str comparison
            TEST_CSTR_CPR__FL(filename, linenum,
                              reinterpret_cast<const char *>(&recorded_tx_data[ii][0]),
                              expected[ii],
                              "send(%s).chain: [%s] repeated_run=%zu send_data_index=%zu %s",
                              data, description, i, ii, expected[ii]);
        }

        recorded_tx_data.clear();
        check_pointers(filename, linenum, expected_mem_counter, data, i, description, false);
    }
}

#define TEST_SEND_FILTER(s, c, ...) test_send_filter_impl(__FILE__, __LINE__, s, c, #c, __VA_ARGS__)

TEST_CASE(test_send_filters)
{
    auto point   = interface::filters::forward_by_pointing<max_packet_size>();
    auto add111  = interface::filters::append<max_packet_size>("111");
    auto add222  = interface::filters::append<max_packet_size>("222");
    auto addAAAA = interface::filters::append<max_packet_size>("AAAA");
    auto add333  = interface::filters::append<max_packet_size>("333");
    auto add444  = interface::filters::append<max_packet_size>("444");
    auto split2  = interface::filters::enforce_fixed_size<2, max_packet_size>();
    auto split3  = interface::filters::enforce_fixed_size<3, max_packet_size>();
    auto join20  = interface::filters::enforce_fixed_size<20, max_packet_size>();

    // test arguments:
    //   TEST_SEND_FILTER(<initial send>, <filters>, <expected outputs>, <expected pointers>, <number of loops>)
    //
    TEST_SEND_FILTER("000", point, expected_outputs{"000"}, pointer_indexes{0, 0}); // the input pointer 0, is chained to the output pointer 0 (no copies were made)
    TEST_SEND_FILTER("A0B0C00", add111.chain(add222).chain(add333).chain(split2), expected_outputs{"A0", "B0", "C0", "01", "11", "22", "23", "33"},
                     // NOTE: enforce_fixed_size scans through the split pointer's source, hence the alternating sequentially-increasing ptr, and fixed ptr
                     pointer_indexes{0, 1, 1, 1, 1, 1, 1, 2, 3, 2, 4, 2, 5, 2, 6, 2, 7, 2, 8, 2, 9, 2});
    TEST_SEND_FILTER("000", add111.chain(add222).chain(add333), expected_outputs{"000111222333"},
                     pointer_indexes{0, 1, 1, 1, 1, 1});
    TEST_SEND_FILTER("000", add111.chain(add222).chain(add333).chain(split3), expected_outputs{"000", "111", "222", "333"},
                     pointer_indexes{0, 1, 1, 1, 1, 1, 1, 2, 3, 2, 4, 2, 5, 2});
    TEST_SEND_FILTER("000", split3, expected_outputs{"000"},
                     pointer_indexes{0, 1});
    TEST_SEND_FILTER("000", add111.chain(addAAAA).chain(add333).chain(addAAAA).chain(split3), expected_outputs{"000", "111", "AAA", "A33", "3AA"},
                     pointer_indexes{0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 2, 4, 2, 5, 2, 6, 2, 7, 8},
                     1);
    TEST_SEND_FILTER("000", add111.chain(split3), expected_outputs{"000", "111"},
                     pointer_indexes{0, 1, 1, 2, 3, 2});
    TEST_SEND_FILTER("000111222333", split3, expected_outputs{"000", "111", "222", "333"},
                     pointer_indexes{0, 1, 2, 1, 3, 1, 4, 1});
    TEST_SEND_FILTER("000111222", add333.chain(split3), expected_outputs{"000", "111", "222", "333"},
                     pointer_indexes{0, 1, 1, 2, 3, 2, 4, 2, 5, 2});
    TEST_SEND_FILTER("XXX111222", add333.chain(split3).chain(add444), expected_outputs{"XXX444", "111444", "222444", "333444"},
                     pointer_indexes{0, 1, 1, 2, 2, 2, 3, 2, 2, 2, 4, 2, 2, 2, 5, 2, 2, 2});
    TEST_SEND_FILTER("000", add111.chain(add222).chain(add333).chain(split3).chain(add444), expected_outputs{"000444", "111444", "222444", "333444"},
                     pointer_indexes{0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 3, 2, 2, 2, 4, 2, 2, 2, 5, 2, 2, 2});
    TEST_SEND_FILTER("000", add111.chain(add222).chain(add333).chain(split3).chain(add444).chain(join20), expected_outputs{"00044411144422244433"},
                     pointer_indexes{0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 4, 2, 2, 2, 2, 3, 5, 2, 2, 2, 2, 3, 6, 2, 2, 2, 2, 7, 8, 3},
                     1);
}

template <size_t rx_size, size_t N, typename T>
void test_receive_filter_impl(const char *filename, int linenum, const char (&in_data)[N], T test_filter, const char *description,
                              std::vector<const char *> &&expected, std::vector<int> &&expected_mem_counter = {}, size_t test_times = 3)
{
    {
        std::lock_guard<std::mutex> lk(desired_rx_data_mutex);
        desired_rx_data.clear();
        for (auto item : in_data)
            if (item != 0)
                desired_rx_data.push_back(item);
    }

    demo::passthrough server(
        interface::common_opts()
            .receive_filter(&test_filter));

    thread_printf("\n>>> test_times=%zu: %s: is_open=%i\n", test_times, description, server.is_open());
    for (size_t i = 0; i < test_times; ++i)
    {
        uint8_t data[rx_size] = {};
        for (size_t ii = 0; ii < expected.size(); ii++)
        {
            auto rx_info = server.receive(data);
            display_receive_data(data, rx_info.size, "final");
            data[rx_info.size] = 0;
            TEST_CSTR_CPR__FL(filename, linenum,
                              reinterpret_cast<const char *>(data),
                              expected[ii],
                              "receive(%s).chain: [%s] repeated_run=%zu send_data_index=%zu %s: %s",
                              data, description, i, ii, expected[ii], rx_info.status.c_str());
        }
        check_pointers(filename, linenum, expected_mem_counter, data, i, description, true);
    }
}

#define TEST_RECV_FILTER(s, c, ...)                                                                \
    do                                                                                             \
    {                                                                                              \
        test_receive_filter_impl<max_packet_size>(__FILE__, __LINE__, s, c, #c, __VA_ARGS__);      \
        test_receive_filter_impl<max_packet_size - 1u>(__FILE__, __LINE__, s, c, #c, __VA_ARGS__); \
    } while (0)

TEST_CASE(test_receive_filters)
{
    auto add111  = interface::filters::append<max_packet_size>("111");
    auto add222  = interface::filters::append<max_packet_size>("222");
    auto addAAAA = interface::filters::append<max_packet_size>("AAAA");
    auto add333  = interface::filters::append<max_packet_size>("333");
    auto add444  = interface::filters::append<max_packet_size>("444");
    auto split3  = interface::filters::enforce_fixed_size<3, max_packet_size>();
    auto join20  = interface::filters::enforce_fixed_size<20, max_packet_size>();

    TEST_RECV_FILTER("000", add111, {"000111"}, pointer_indexes{0, 0});              // the input pointer 0, is chained to the output pointer 0 (no copies were made)
    TEST_RECV_FILTER("000111", split3, {"000", "111"}, pointer_indexes{0, 1, 2, 1}); // the shift from 0 to 2 as an input us due to split shifting the input (so not actually a third memory array)
    TEST_RECV_FILTER("000", add111.chain(add222).chain(add333), {"000111222333"}, pointer_indexes{0, 0, 0, 0, 0, 0});
    TEST_RECV_FILTER("000", add111.chain(add222).chain(add333).chain(split3), {"000", "111", "222", "333"},
                     pointer_indexes{0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 3, 1, 4, 1});
    TEST_RECV_FILTER("000111", split3, {"000", "111"},
                     pointer_indexes{0, 1, 2, 1});
    TEST_RECV_FILTER("000", add111.chain(addAAAA).chain(add333).chain(addAAAA).chain(split3), {"000", "111", "AAA", "A33", "3AA"},
                     pointer_indexes{0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 3, 1, 4, 1, 5, 1}, 1);
    TEST_RECV_FILTER("000", add111.chain(split3), {"000", "111"},
                     pointer_indexes{0, 0, 0, 1, 2, 1});
    TEST_RECV_FILTER("000111222333", split3, {"000", "111", "222", "333"},
                     pointer_indexes{0, 1, 2, 1, 3, 1, 4, 1});
    TEST_RECV_FILTER("000111222", add333.chain(split3), {"000", "111", "222", "333"},
                     pointer_indexes{0, 0, 0, 1, 2, 1, 3, 1, 4, 1});
    TEST_RECV_FILTER("000111222", add333.chain(split3).chain(add444), {"000444", "111444", "222444", "333444"},
                     pointer_indexes{0, 0, 0, 1, 1, 1, 2, 1, 1, 1, 3, 1, 1, 1, 4, 1, 1, 1});
    TEST_RECV_FILTER("000", add111.chain(add222).chain(add333).chain(split3).chain(add444), {"000444", "111444", "222444", "333444"},
                     pointer_indexes{0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 1, 1, 1, 3, 1, 1, 1, 4, 1, 1, 1});
    TEST_RECV_FILTER("000", add111.chain(add222).chain(add333).chain(split3).chain(add444).chain(join20), {"00044411144422244433"},
                     pointer_indexes{0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 3, 1, 1, 1, 1, 2, 4, 1, 1, 1, 1, 2, 5, 1, 1, 1, 1, 6}, 1);
    TEST_RECV_FILTER("hello1\xC0\xC0hello2\xC0hello3\xC0", interface::filters::slip::decode<max_packet_size>(), {"hello1", "hello2", "hello3"},

                     // NOTE: decode skips through the pointer's source array, hence the alternating sequentially-increasing ptr, and fixed ptr
                     // there are not actually more than two arrays being used here
                     pointer_indexes{0, 1, 2, 1, 3, 1});
}
