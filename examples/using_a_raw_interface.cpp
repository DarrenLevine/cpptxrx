/// @brief This example demonstrates how an already wrapped raw cpptxrx UDP interface can be used
#include "../include/default_udp_raw.h"
#include <stdio.h>

static void send_demonstrating_polymorphism(interface::abstract &connection)
{
    const uint8_t tx_data[] = "hello 2!";
    auto send_status        = connection.send(tx_data, sizeof(tx_data));
    printf("polymorphic send: \"%s\" (size=%zu bytes, status=%s)\n",
           tx_data, sizeof(tx_data), send_status.c_str());
}

int main()
{
    // How to construct AND open the connection simultaneously:
    udp::socket_raw server(udp::socket_raw::opts()
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
    //      printf("server is open (%s)\n", server.open_status().c_str());
    //  4) or, via checking it as a boolean
    if (server)
        printf("server(%s) is open!\n", server.name());
    else
        printf("server open error: %s (%s)\n",
               server.open_status().c_str(),
               std::strerror(server.open_status().get_error_code()));

    // A interfaces can also be created without opening on construction
    udp::socket_raw client;

    // We expect the connection to not be open yet
    printf("client not open yet (%s)\n", client.open_status().c_str());

    // Call open with any open arguments you want
    auto status = client.open(udp::socket_raw::opts()
                                  .role(udp::role_e::CLIENT)
                                  .port(1234)
                                  .ipv4_address("127.0.0.1"));

    // Since it wasn't opened in the constructor, you can check the return status directly instead of
    // using ".open_status()"
    printf("client is now open (%s)\n", status.c_str());

    // A interface can be closed as easily
    status = client.close();
    printf("Close operation status = %s, Current client status = %s\n", status.c_str(), client.open_status().c_str());

    // Now that there are default options are stored, you can call reopen the connection using the last
    // arguments at any time or specify new arguments.
    // Note: In this instance, .open() would also work. The difference between ".open()" and ".reopen()"
    //       is that if ".open()" is called while the interface is already open it will fail, but if
    //       ".reopen()" is called while the interface is already open, it will first be closed.
    status = client.reopen();

    // and you can double check what open arguments were used, even if the open operation modified them
    udp::socket_raw::opts open_args;
    if (client.get_open_args(open_args))
        printf("client is now open (%s) using old arguments (port=%i)\n",
               status.c_str(), int(open_args.m_port));

    // If you want to not reopen an already open connection, then use open instead of reopen:
    printf("client refused an open() request, since it was already open (%s)\n", client.open().c_str());

    // you can also send and receive using the same API as the threadsafe
    {
        const uint8_t tx_data[] = "hello 1!";
        auto send_status        = client.send(tx_data);
        printf("client sent: \"%s\" (size=%zu bytes, status=%s)\n",
               tx_data, sizeof(tx_data), send_status.c_str());
    }

    // and access polymorphically
    send_demonstrating_polymorphism(client);

    {
        uint8_t rx_data[100] = {};
        auto rx_result_info  = server.receive(rx_data, sizeof(rx_data));
        printf("server received: \"%s\" (size=%zu bytes, status=%s)\n",
               rx_data, rx_result_info.size, rx_result_info.status.c_str());

        const uint8_t tx_data[] = "hello 3!";
        auto send_status        = server.send(tx_data, sizeof(tx_data));
        printf("server sent: \"%s\" (size=%zu bytes, status=%s)\n",
               tx_data, sizeof(tx_data), send_status.c_str());
    }
    {
        uint8_t rx_data[100] = {};
        auto rx_result_info  = client.receive(rx_data, sizeof(rx_data));
        printf("client received: \"%s\" (size=%zu bytes, status=%s)\n",
               rx_data, rx_result_info.size, rx_result_info.status.c_str());
    }

    {
        uint8_t rx_data[100] = {};
        auto rx_result_info  = server.receive(rx_data, sizeof(rx_data));
        printf("server received: \"%s\" (size=%zu bytes, status=%s)\n",
               rx_data, rx_result_info.size, rx_result_info.status.c_str());
    }
}
