/// @brief This example demonstrates how an already wrapped threadsafe cpptxrx TCP interface can be used.
/// It has it's own dedicated example because TCP needs to manage multiple connections (clients) on different
/// channels and is a byte stream protocol, rather than a packet based protocol. Making this a great opportunity
/// to show off how CppTXRx can abstract away and work with those unique features.

// importing thread_printf for thread safe printing, and some test utilities to make the example explicit in it's expectations
#include "utils/test_utils.h"

// importing a pre-made TCP socket for this demo
#include "../include/default_tcp.h"

using namespace ctr;

TEST_CASE(using_a_tcp_interface_example)
{
    // TCP server example:
    static constexpr size_t max_clients = 100;
    tcp::socket<tcp::role_e::SERVER, max_clients> server(

        // opts are connection-specific options
        tcp::opts()
            .port(2240)
            .ipv4_address("127.0.0.1"),

        // common_opts are options that can be use on any interface
        common_opts()
            // to make this server more robust, if the server closes for some
            // reason other than a user-request, try to reopen it after 250ms
            .auto_reopen_after(std::chrono::milliseconds(250))

            // since TCP is a byte stream protocol, we can use a SLIP encoder/decoder
            // to segment the byte stream back into packets
            .receive_filter(allow_heap(slip::decode()))
            .send_filter(allow_heap(slip::encode())));

    thread_printf("%s open status: %s, error info: %s\n",
                  server.name(),
                  server.open_status().c_str(),
                  server.open_status().error_c_str());
    TEST_CSTR_CPR(server.open_status().c_str(), "SUCCESS");

    // next we'll create a client within a scope, so that it can later demonstrate destruction by going out of scope
    {
        tcp::socket<tcp::role_e::CLIENT> client(
            tcp::opts() // opts are connection-specific options
                .port(2240)
                .ipv4_address("127.0.0.1"),
            common_opts() // common_opts are options that can be use on any interface
                .receive_filter(allow_heap(slip::decode()))
                .send_filter(allow_heap(slip::encode())));

        thread_printf("%s open status: %s, error info: %s\n",
                      client.name(),
                      client.open_status().c_str(),
                      client.open_status().error_c_str());
        TEST_CSTR_CPR(client.open_status().c_str(), "SUCCESS");
        TEST_CSTR_CPR(client.open_status().c_str(), "SUCCESS");

        // A interface can be closed as easily
        {
            auto status = client.close();
            thread_printf("client close status: %s, error info: %s\n", status.c_str(), status.error_c_str());
            TEST_CSTR_CPR(client.open_status().c_str(), "NOT_OPEN");
        }

        {
            uint8_t rx_data[default_max_packet_size] = {};

            // the server will time out trying to receive, waiting for the nonexistent clients to connect
            auto rx_result = server.receive(rx_data, std::chrono::milliseconds(100));
            thread_printf("server recv: '%s' (size=%zu bytes, status=%s, error info=%s)\n",
                          rx_data, rx_result.size, rx_result.status.c_str(), rx_result.status.error_c_str());
            TEST_CSTR_CPR(rx_result.status.c_str(), "TIMED_OUT");

            // sending won't work while there are no clients
            const uint8_t tx_data[] = "<this won't be sent since there are no open clients>";
            auto send_status        = server.send(tx_data, std::chrono::milliseconds(100));
            thread_printf("server sent: '%s' (size=%zu bytes, status=%s, error info=%s)\n",
                          tx_data, sizeof(tx_data), send_status.c_str(), send_status.error_c_str());
            TEST_CSTR_CPR(send_status.c_str(), "SEND_FAILED_NO_CLIENTS");
        }

        // Now that there are default options are stored, you can call reopen the connection using the last
        // arguments at any time or specify new arguments.
        // Note: In this instance, .open() would also work. The difference between ".open()" and ".reopen()"
        //       is that if ".open()" is called while the interface is already open it will fail, but if
        //       ".reopen()" is called while the interface is already open, it will first be closed.
        {
            thread_printf("client calling reopen() with last open arguments.\n");
            auto status = client.reopen();
            TEST_CSTR_CPR(status.c_str(), "SUCCESS");

            // and you can double check what open arguments were used, even if the open operation modified them
            tcp::opts open_args;
            if (client.get_open_args(open_args))
                thread_printf("client is now open (%s) using old arguments (port=%i)\n", status.c_str(), int(open_args.m_port));
            TEST_EQ(int(open_args.m_port), 2240);
        }

        // now, to demonstrate the thread safety, send and receive simultaneously multiple
        // times from multiple threads:
        {
            // receive in one thread
            raii_thread receiving_thread(
                [&]()
                {
                    uint8_t rx_data[default_max_packet_size] = {};
                    auto rx_result                           = client.receive(rx_data);
                    thread_printf("<- client received: '%s' (size=%zu bytes, status=%s, error info=%s)\n",
                                  rx_data, rx_result.size, rx_result.status.c_str(), rx_result.status.error_c_str());
                    IN_THREAD(TEST_CSTR_CPR(rx_result.status.c_str(), "SUCCESS"));
                });

            // send from another thread
            raii_thread sending_thread(
                [&]()
                {
                    // but wait a bit before sending
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    const uint8_t tx_data[] = "<data to send across multiple threads>";
                    auto send_status        = server.send(tx_data);
                    thread_printf("-> server sent: '%s' (size=%zu bytes, status=%s, error info=%s)\n",
                                  tx_data, sizeof(tx_data), send_status.c_str(), send_status.error_c_str());
                    IN_THREAD(TEST_CSTR_CPR(send_status.c_str(), "SUCCESS"));
                });

            // or in the same thread, you can send a few times
            size_t packets_sent = 6;
            for (size_t i = 0; i < packets_sent; i++)
            {
                uint8_t tx_data[] = "tx_server_X";
                tx_data[10]       = 48 + i;
                auto send_status  = server.send(tx_data);
                thread_printf("-> server sent: '%s' (size=%zu bytes, status=%s, error info=%s)\n",
                              tx_data, sizeof(tx_data), send_status.c_str(), send_status.error_c_str());
                TEST_CSTR_CPR(send_status.c_str(), "SUCCESS");
            }

            // and then receive all the packets previously sent
            // NOTE: the reason this works reliably at high speed with TCP is because of the SLIP filters,
            //       without SLIP, the small packets would get combined and while all bytes would get transmitted
            //       the boundaries of each packet would get mangled.
            for (size_t i = 0; i < packets_sent; i++)
            {
                uint8_t rx_data[default_max_packet_size] = {};

                auto rx_result = client.receive(rx_data, std::chrono::milliseconds(1000));
                thread_printf("<- client received: '%s' (size=%zu, status=%s, error info=%s)\n",
                              rx_data, rx_result.size, rx_result.status.c_str(), rx_result.status.error_c_str());
                TEST_CSTR_CPR(rx_result.status.c_str(), "SUCCESS");
            }
        }

        // when the client goes out of scope here, it will disconnect from the sever
    }

    // here's a demo of multiple clients can receive and send data to the server
    {
        constexpr size_t number_of_clients = 30;
        tcp::socket<tcp::role_e::CLIENT> clients[number_of_clients];
        for (auto &c : clients)
        {
            auto status = c.open(
                tcp::opts()
                    .port(2240)
                    .ipv4_address("127.0.0.1"),
                common_opts()
                    .receive_filter(allow_heap(filters::slip::decode()))
                    .send_filter(allow_heap(filters::slip::encode())));
            TEST_CSTR_CPR(status.c_str(), "SUCCESS");
        }

        uint8_t counter = '+';
        for (auto &c : clients)
        {
            uint8_t tx_data[] = {'a', 'b', ++counter, 0u};
            thread_printf("client sending = '%s'\n", tx_data);
            auto status = c.send(tx_data, std::chrono::milliseconds(1000));
            TEST_CSTR_CPR(status.c_str(), "SUCCESS");
        }

        for (size_t i = 0; i < number_of_clients; i++)
        {
            uint8_t rx_data[default_max_packet_size] = {};

            auto rx_result = server.receive(rx_data, std::chrono::milliseconds(1000));
            thread_printf("server received = '%s' (status=%s, error info=%s)\n",
                          rx_data, rx_result.status.c_str(), rx_result.status.error_c_str());
            TEST_CSTR_CPR(rx_result.status.c_str(), "SUCCESS");
        }

        // here a few clients are closed, to show that the server will auto-detect they've closed, and no longer send to them
        clients[3].close();
        clients[20].close();
        clients[21].close();

        TEST_CSTR_CPR(clients[3].open_status().c_str(), "NOT_OPEN");
        TEST_CSTR_CPR(clients[20].open_status().c_str(), "NOT_OPEN");
        TEST_CSTR_CPR(clients[21].open_status().c_str(), "NOT_OPEN");

        // then the server can send to the still open clients:
        {
            uint8_t tx_data[] = "response_text_abc";
            auto send_status  = server.send(tx_data, std::chrono::milliseconds(1000));
            thread_printf("server sent: '%s' (size=%zu bytes, status=%s, error info=%s)\n",
                          tx_data, sizeof(tx_data), send_status.c_str(), send_status.error_c_str());
            TEST_CSTR_CPR(send_status.c_str(), "SUCCESS");
        }

        // and we can see that the open clients receive the data, while the closed ones fail with "NOT_OPEN"
        for (size_t client_index = 0; client_index < number_of_clients; client_index++)
        {
            uint8_t rx_data[default_max_packet_size] = {};

            auto rx_result = clients[client_index].receive(rx_data, std::chrono::milliseconds(100));
            thread_printf("client received = '%s' (%s) channel=%i\n", rx_data, rx_result.status.c_str(), rx_result.channel);
            if (client_index == 3 || client_index == 20 || client_index == 21)
                TEST_CSTR_CPR(rx_result.status.c_str(), "NOT_OPEN");
            else
                TEST_CSTR_CPR(rx_result.status.c_str(), "SUCCESS");
        }

        // next we'll close all the clients to see what happens on the server end
        thread_printf("closing all clients\n");
        for (size_t client_index = 0; client_index < number_of_clients; client_index++)
        {
            auto status = clients[client_index].close();
            if (client_index == 3 || client_index == 20 || client_index == 21)
                TEST_CSTR_CPR(status.c_str(), "NOT_OPEN");
            else
                TEST_CSTR_CPR(status.c_str(), "SUCCESS");
        }

        // since unix sockets learn about client closures by doing a receive (https://www.greenend.org.uk/rjk/tech/poll.html),
        // we can get the server to see the client closures by doing a receive here.
        // Alternatively, the better practice is to continuously receive by using a ".receive_callback()", since then
        // closures and data transfers will occur immediately as they become available, and you won't risk not looking at
        // the receive for long enough to overflow the kernel's max receive buffer size (see your "SO_RCVBUF" setting,
        // to see how large that is on your system, it's likely around 128 kB).

        // since no clients are available, we'll get a status == TIMED_OUT here
        uint8_t rx_data[default_max_packet_size - 1] = {};
        auto rx_result                               = server.receive(rx_data, std::chrono::milliseconds(100));
        thread_printf("server receive: '%s'[%zu bytes] (status=%s, error info=%s)\n",
                      rx_data, rx_result.size, rx_result.status.c_str(), rx_result.status.error_c_str());
        TEST_CSTR_CPR(rx_result.status.c_str(), "TIMED_OUT");

        // and sending indicates status == SEND_FAILED_NO_CLIENTS
        uint8_t tx_data[] = "failed send data";
        auto send_status  = server.send(tx_data, std::chrono::milliseconds(200));
        thread_printf("server sent: '%s' (size=%zu bytes, status=%s, error info=%s)\n",
                      tx_data, sizeof(tx_data), send_status.c_str(), send_status.error_c_str());
        TEST_CSTR_CPR(send_status.c_str(), "SEND_FAILED_NO_CLIENTS");
    }

    tcp::socket<tcp::role_e::CLIENT> client2(
        tcp::opts()
            .port(2240)
            .ipv4_address("127.0.0.1"),
        common_opts()
            .receive_filter(allow_heap(filters::slip::decode()))
            .send_filter(allow_heap(filters::slip::encode())));
    TEST_CSTR_CPR(client2.open_status().c_str(), "SUCCESS");

    // you can also .close() a socket even if multiple threads are actively trying to send or receive,
    // the active operations will be immediately interrupted and canceled

    // here we create a thread that will continuously receive, so that we can see it exits automatically
    // once the server is closed
    raii_thread thread5(
        [&]()
        {
            do
            {
                uint8_t rx_data[100] = {};
                auto rx_result       = server.receive(rx_data, sizeof(rx_data), std::chrono::seconds(30));
                thread_printf("server received: '%s' (size=%zu bytes, status=%s, error info=%s)\n",
                              rx_data, rx_result.size, rx_result.status.c_str(),
                              rx_result.status.error_c_str());
                thread_printf("server open status: %s (%s)\n", server.open_status().c_str(),
                              server.open_status().error_c_str());
                TEST_CSTR_CPR(rx_result.status.c_str(), "NOT_OPEN");
                TEST_CSTR_CPR(server.open_status().c_str(), "NOT_OPEN");
            } while (server);
        });

    // closing the server in another thread
    {
        raii_thread thread6(
            [&]()
            {
                // delay a bit so that other threads are doing operations while the close occurs
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                thread_printf("closing server from another thread\n");
                auto status = server.close();
                thread_printf("server close status=%s, error info=%s)\n", status.c_str(), status.error_c_str());
                TEST_CSTR_CPR(status.c_str(), "SUCCESS");
                TEST_CSTR_CPR(server.open_status().c_str(), "NOT_OPEN");
            });
    }

    // after the server closes, then operations in the client should start failing
    {
        uint8_t rx_data[default_max_packet_size] = {};

        auto rx_result = client2.receive(rx_data, std::chrono::seconds(1));
        thread_printf("client2 received: '%s' (size=%zu bytes, status=%s, error info=%s)\n",
                      rx_data, rx_result.size, rx_result.status.c_str(),
                      rx_result.status.error_c_str());
        TEST_CSTR_CPR(rx_result.status.c_str(), "RECV_FAILED_SOCK_CLOSED");
        TEST_EQ(bool(client2), false);

        // since the client now knows it's closed, further operations will fail
        auto tx_status2 = client2.send("data", std::chrono::seconds(1));
        thread_printf("client2 send status=%s, error info=%s\n", tx_status2.c_str(), tx_status2.error_c_str());
        TEST_CSTR_CPR(tx_status2.c_str(), "NOT_OPEN");
    }
}