#include "../examples/utils/printing.h"
#include "../include/default_udp.h"

#define fail_and_exit(...)                \
    do                                    \
    {                                     \
        debug_printf(__VA_ARGS__);        \
        thread_printf("Failed test!!\n"); \
        exit(EXIT_FAILURE);               \
    } while (0)

static void test_no_ops()
{
    udp::socket client1;
}
static void test_open_then_destroy()
{
    {
        udp::socket client1(udp::socket::opts()
                                .role(udp::role_e::CLIENT)
                                .port(1234)
                                .ipv6_address("::ffff:127.0.0.1"));
    }
    {
        udp::socket server2(udp::socket::opts()
                                .role(udp::role_e::SERVER)
                                .port(1234)
                                .ipv6_address("::ffff:127.0.0.1"));
    }
}
enum class test_case_e : size_t
{
    ALLOW_DESTRUCTION,
    CLOSE_SERVER_DURING_RX,
    DESTROY_SERVER_DURING_RX,
    TIMEOUT_SERVER_DURING_RX,
    NUM_TEST_CASES
};

static void test_send_receive_then_closure_case(test_case_e test_case, size_t loop_iteration)
{
    udp::socket server(udp::socket::opts()
                           .role(udp::role_e::SERVER)
                           .port(1230)
                           .ipv6_address("::ffff:127.0.0.1"));
    udp::socket client(udp::socket::opts()
                           .role(udp::role_e::CLIENT)
                           .port(1230)
                           .ipv6_address("::ffff:127.0.0.1"));

    auto s_stat = server.open_status(), c_stat = client.open_status();
    if (s_stat != interface::status_e::SUCCESS)
        fail_and_exit("server open error: %s:%s\n", s_stat.c_str(), std::strerror(s_stat.get_error_code()));
    if (c_stat != interface::status_e::SUCCESS)
        fail_and_exit("client open error: %s:%s\n", c_stat.c_str(), std::strerror(c_stat.get_error_code()));

    if (!client.is_open() || !server.is_open())
        fail_and_exit("FAILURE!\n");

    {
        interface::raii_thread thread1(
            [&]()
            {
                uint8_t tx_data[] = "hello 1!";
                auto send_status  = client.send(tx_data, sizeof(tx_data));
                if (send_status != interface::status_e::SUCCESS)
                    fail_and_exit("client send error: \"%s\" (size=%zu bytes, status=%s)\n", tx_data, sizeof(tx_data), send_status.c_str());
            });

        interface::raii_thread thread2(
            [&]()
            {
                uint8_t tx_data[] = "hello 2!";
                auto send_status  = client.send(tx_data, sizeof(tx_data));
                if (send_status != interface::status_e::SUCCESS)
                    fail_and_exit("client send error: \"%s\" (size=%zu bytes, status=%s)\n", tx_data, sizeof(tx_data), send_status.c_str());

                uint8_t rx_data[100] = {};
                auto rx_result_info  = client.receive(rx_data, sizeof(rx_data));
                if (rx_result_info.status != interface::status_e::SUCCESS)
                    fail_and_exit("client receive error: \"%s\" (size=%zu bytes, status=%s)\n", rx_data, rx_result_info.size, rx_result_info.status.c_str());
            });

        interface::raii_thread thread3(
            [&]()
            {
                uint8_t rx_data[100] = {};
                auto rx_result_info  = server.receive(rx_data, sizeof(rx_data));
                if (rx_result_info.status != interface::status_e::SUCCESS)
                    fail_and_exit("server receive error: \"%s\" (size=%zu bytes, status=%s)\n", rx_data, rx_result_info.size, rx_result_info.status.c_str());

                uint8_t tx_data[] = "hello 3!";
                auto send_status  = server.send(tx_data, sizeof(tx_data));
                if (send_status != interface::status_e::SUCCESS)
                    fail_and_exit("server send error: \"%s\" (size=%zu bytes, status=%s)\n", tx_data, sizeof(tx_data), send_status.c_str());
            });

        interface::raii_thread thread4(
            [&]()
            {
                uint8_t rx_data[100] = {};
                auto rx_result_info  = server.receive(rx_data, sizeof(rx_data));
                if (rx_result_info.status != interface::status_e::SUCCESS)
                    fail_and_exit("server receive error: \"%s\" (size=%zu bytes, status=%s)\n", rx_data, rx_result_info.size, rx_result_info.status.c_str());
            });
    }

    switch (test_case)
    {
    case test_case_e::ALLOW_DESTRUCTION:
        return;
    case test_case_e::CLOSE_SERVER_DURING_RX:
    {
        interface::raii_thread thread5(
            [&]()
            {
                if ((loop_iteration % 1000) < 500) // adds a time-sweep to the test
                    std::this_thread::sleep_for(std::chrono::microseconds(50 - loop_iteration % 50));
                uint8_t rx_data[100] = {};
                auto rx_result_info  = server.receive(rx_data, sizeof(rx_data), std::chrono::seconds(300));
                if (rx_result_info.status != interface::status_e::NOT_OPEN)
                    fail_and_exit("server receive error: \"%s\" (size=%zu bytes, status=%s)\n",
                                  rx_data, rx_result_info.size, rx_result_info.status.c_str());
            });
        interface::raii_thread thread6(
            [&]()
            {
                if ((loop_iteration % 1000) < 500) // adds a time-sweep to the test
                    std::this_thread::sleep_for(std::chrono::microseconds(loop_iteration % 50));
                server.close();
            });
        break;
    }
    case test_case_e::DESTROY_SERVER_DURING_RX:
    {
        interface::raii_thread thread5(
            [&]()
            {
                if ((loop_iteration % 1000) < 500) // adds a time-sweep to the test
                    std::this_thread::sleep_for(std::chrono::microseconds(50 - loop_iteration % 50));
                uint8_t rx_data[100] = {};
                auto rx_result_info  = server.receive(rx_data, sizeof(rx_data), std::chrono::seconds(300));
                if (rx_result_info.status != interface::status_e::CANCELED_IN_DESTROY)
                    fail_and_exit("server receive error: \"%s\" (size=%zu bytes, status=%s)\n",
                                  rx_data, rx_result_info.size, rx_result_info.status.c_str());

                auto tx_status = server.send(rx_data, 1);
                if (tx_status != interface::status_e::CANCELED_IN_DESTROY)
                    fail_and_exit("server send status=%s\n", tx_status.c_str());

                auto open_status = server.open();
                if (open_status != interface::status_e::CANCELED_IN_DESTROY)
                    fail_and_exit("server send status=%s\n", open_status.c_str());
            });
        interface::raii_thread thread6(
            [&]()
            {
                if ((loop_iteration % 1000) < 500) // adds a time-sweep to the test
                    std::this_thread::sleep_for(std::chrono::microseconds(loop_iteration % 50));
                server.destroy();
            });
        break;
    }
    case test_case_e::TIMEOUT_SERVER_DURING_RX:
    {
        uint8_t rx_data[100]     = {};
        auto timeout_duration_ms = std::chrono::milliseconds(loop_iteration % 10);
        auto start_time          = std::chrono::steady_clock::now();
        auto rx_result_info      = server.receive(rx_data, sizeof(rx_data), timeout_duration_ms);
        auto elapsed_time        = std::chrono::steady_clock::now() - start_time;
        if (rx_result_info.status != interface::status_e::TIMED_OUT)
            fail_and_exit("server receive error: \"%s\" (size=%zu bytes, status=%s)\n",
                          rx_data, rx_result_info.size, rx_result_info.status.c_str());
        auto ms_diff = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed_time - timeout_duration_ms).count());
        // can set worst_computer_accuracy_ms=0, but different computers may have different timing accuracies and/or more demanding parallel CPU loadings,
        // so instead we're just checking for blatant disregard for the timeout instead of perfect accuracy
        int worst_computer_accuracy_ms = 60;
        if (abs(ms_diff) > worst_computer_accuracy_ms)
            fail_and_exit("server receive timeout had an error of %i, for a timeout of %i ms\n", ms_diff, static_cast<int>(timeout_duration_ms.count()));
        break;
    }
    case test_case_e::NUM_TEST_CASES:
        break;
    default:
        break;
    }
}

static void test_send_receive_then_closures(size_t loop_iteration)
{
    for (size_t i = 0; i < static_cast<size_t>(test_case_e::NUM_TEST_CASES); i++)
        test_send_receive_then_closure_case(static_cast<test_case_e>(i), loop_iteration);
}

int main()
{
    size_t total_tests = 1000;
    auto start_time    = std::chrono::steady_clock::now();
    thread_printf("Starting tests.\n"); // if we got here, the tests passed
    for (size_t i = 0; i <= total_tests; i++)
    {
        if (i % 100 == 0)
            thread_printf("| running stress test %5i/%i\n", static_cast<int>(i), static_cast<int>(total_tests));
        test_no_ops();
        test_open_then_destroy();
        test_send_receive_then_closures(i);
    }
    auto elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
    thread_printf("Passed tests! In %.3f seconds\n", elapsed_time_ms.count() / 1000.); // if we got here, the tests passed
}