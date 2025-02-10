/// @brief This example walks through the basics of how an existing threadsafe cpptxrx class can
// be used: send(), receive(), open(), close(), reopen(), get_open_args(), etc.
//
// NOTE: This example is nearly the same as the "02_using_a_raw_interface.cpp" example,
//       but with one difference: The "raw" API isn not thread safe, so multiple threads are not used.
//                                While the "threadsafe" API is thread safe, so multiple threads are used.

#include "../include/default_udp.h"
#include "utils/printing.h" // defines thread_printf for thread safe printing

int main()
{
    // How to construct AND open the connection simultaneously:
    udp::socket server(udp::socket::opts()
                           .role(udp::role_e::SERVER)
                           .port(1234)
                           .ipv4_address("127.0.0.1"));

    // We expect the connection to now be open, there are several ways to check the status of the last
    // run open operation:
    //  1) via enum comparison on the last open status:
    //      bool is_open1 = server.open_status() == interface::status_e::SUCCESS;
    //  2) via ".is_open()":
    //      bool is_open2 = server.is_open();
    //  3) via the enum's char* human readable name:
    //      thread_printf("server is open (%s)\n", server.open_status().c_str());
    //  4) or, via checking it as a boolean
    if (server)
        thread_printf("server(%s) is open!\n", server.name());
    else
        thread_printf("server open error: %s (%s)\n", server.open_status().c_str(), server.open_status().error_c_str());

    // A interfaces can also be created without opening on construction
    udp::socket client;

    // We expect the connection to not be open yet
    thread_printf("client not open yet (%s)\n", client.open_status().c_str());

    // Call open with any open arguments you want
    auto status = client.open(udp::socket::opts()
                                  .role(udp::role_e::CLIENT)
                                  .port(1234)
                                  .ipv4_address("127.0.0.1"));

    // Since it wasn't opened in the constructor, you can check the return status directly instead of
    // using ".open_status()"
    thread_printf("client is now open (%s)\n", status.c_str());

    // A interface can be closed as easily
    status = client.close();
    thread_printf("Close operation status = %s, Current client status = %s\n",
                  status.c_str(), client.open_status().c_str());

    // Now that there are open options stored (the last open args), you can call reopen the connection
    // using the last arguments at any time or specify new arguments.
    // Note: In this instance, .open() would also work. The difference between ".open()" and ".reopen()"
    //       is that if ".open()" is called while the interface is already open it will fail, but if
    //       ".reopen()" is called while the interface is already open, it will first be closed.
    status = client.reopen();

    // and you can double check what open arguments were used, even if the open operation modified them
    udp::socket::opts open_args;
    if (client.get_open_args(open_args))
        thread_printf("client is now open (%s) using old arguments (port=%i)\n",
                      status.c_str(), int(open_args.m_port));

    // now, to demonstrate the thread safety, send and receive simultaneously multiple times from
    // multiple threads:
    {
        interface::raii_thread thread1(
            [&]()
            {
                const uint8_t tx_data[] = "hello 1!";
                auto send_status        = client.send(tx_data, sizeof(tx_data));
                thread_printf("client sent: \"%s\" (size=%zu bytes, status=%s)\n",
                              tx_data, sizeof(tx_data), send_status.c_str());
            });

        interface::raii_thread thread2(
            [&]()
            {
                const uint8_t tx_data[] = "hello 2!";
                auto send_status        = client.send(tx_data, sizeof(tx_data));
                thread_printf("client sent: \"%s\" (size=%zu bytes, status=%s)\n",
                              tx_data, sizeof(tx_data), send_status.c_str());

                uint8_t rx_data[100] = {};
                auto rx_result_info  = client.receive(rx_data, sizeof(rx_data));
                thread_printf("client received: \"%s\" (size=%zu bytes, status=%s)\n",
                              rx_data, rx_result_info.size, rx_result_info.status.c_str());
            });

        interface::raii_thread thread3(
            [&]()
            {
                uint8_t rx_data[100] = {};
                auto rx_result_info  = server.receive(rx_data, sizeof(rx_data));
                thread_printf("server received: \"%s\" (size=%zu bytes, status=%s)\n",
                              rx_data, rx_result_info.size, rx_result_info.status.c_str());

                const uint8_t tx_data[] = "hello 3!";
                auto send_status        = server.send(tx_data);
                thread_printf("server sent: \"%s\" (size=%zu bytes, status=%s)\n",
                              tx_data, sizeof(tx_data), send_status.c_str());
            });

        interface::raii_thread thread4(
            [&]()
            {
                uint8_t rx_data[100] = {};
                auto rx_result_info  = server.receive(rx_data);
                thread_printf("server received: \"%s\" (size=%zu bytes, status=%s)\n",
                              rx_data, rx_result_info.size, rx_result_info.status.c_str());
            });
    }

    // you can also send and receive without explicitly specifying a length, as long as a length can be deduced
    {
        uint8_t tx_data[] = "some char data";
        server.send(tx_data);

        uint8_t rx_data[sizeof(tx_data)] = {};
        client.receive(rx_data);
        printf("received = \"%s\"\n", rx_data);

        client.send({uint8_t(104), uint8_t(105), uint8_t(0)});
        server.receive(rx_data);
        printf("received = \"%s\"\n", rx_data);
    }

    // you can also .close() a socket even if multiple threads are actively trying to send or receive,
    // the active operations will be immediately interrupted and canceled
    interface::raii_thread thread5(
        [&]()
        {
            uint8_t rx_data[100] = {};
            auto rx_result_info  = server.receive(rx_data, sizeof(rx_data), std::chrono::seconds(300));
            thread_printf("server received: \"%s\" (size=%zu bytes, status=%s)\n",
                          rx_data, rx_result_info.size, rx_result_info.status.c_str());
        });
    interface::raii_thread thread6(
        [&]()
        {
            thread_printf("closing server from another thread\n");
            server.close();
        });

    return 0;
}