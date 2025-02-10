/// @brief This example demonstrates how to define an interface that automatically (and continuously) calls
/// receive() for you within it's management loop, and then passes that received data to a user-specified
/// callback function.
///
/// Side note about performance:
///   Callbacks are an efficient/fast way to receive data, because no thread context
///   switches need to occur in this mode. The receive request, operation, and callback will all occur
///   in the socket's single interface management thread, instead of needing another thread to request
///   one like a manual receive() call would.
///
///   Don't let that scare you away from writing manual receive() calls too much though. Unless you're
///   writing fintech trading code, the extra overhead that threads and their context switches cost
///   (about a few usec: https://www.youtube.com/watch?v=KXuZi9aeGTw) is almost certainly worth it for
///   most real applications, where leveraging the nicer architectures and API construct will make
///   your code easier to reason-about/maintain and safer to use/deploy. Plus, you're probably IO
///   bound if you're using an IO interface anyway, so the context switch overhead is likely not a
///   meaningful slice of your performance budget.
///
///   Anyway, back to the callback example:

// starting with a udp socket for this example
#include "../include/default_udp.h"
#include "utils/printing.h"

// using the shorter ctr namespace for succinctness
using namespace ctr;

// inherit from "receive_callback::base_class" to create a new callback function "type" in a method:
template <size_t max_size = default_max_packet_size>
struct example_callback_t final : receive_callback::base_class<example_callback_t<max_size>, max_size>
{
    void operator()(recv_op &rx_data) final
    {
        thread_printf("Received = [%zu bytes]{\"%s\"}\n",
                      rx_data.received_size, rx_data.received_data.get());
    }
};

// which gives you a type that you can use to instantiate a callback "object"
// (callbacks need instantiation because they need to create a memory array to receive into)
auto example_callback1 = example_callback_t();

// or, more simply "receive_callback::create" will let you create a new callback "type" and
// "object" in one pass, using a callable object as an input, like the lambda function here:
auto example_callback2 = receive_callback::create(
    [](recv_op &rx_data)
    {
        thread_printf("Client received = [%zu bytes]{\"%s\"}\n",
                      rx_data.received_size, rx_data.received_data.get());
    });

int main()
{
    // To set a callback, set the "common_opts().receive_callback" option,
    // either passing in:
    //  1) An externally managed raw pointer to a callback.
    //   or
    //  2) By using allow_heap() to let the socket manage the callback object internally.

    // Here is an example of the second method, where a callback function is
    // both created inline and passed into a socket:
    udp::socket server(
        udp::opts()
            .port(2240)
            .ipv4_address("127.0.0.1")
            .role(udp::role_e::SERVER),
        common_opts()
            .receive_callback(
                allow_heap(receive_callback::create(
                    [](recv_op &rx_data)
                    {
                        thread_printf("Server received = [%zu bytes]{\"%s\"}\n",
                                      rx_data.received_size, rx_data.received_data.get());
                    }))));

    // Here a callback function is simply pointed to, in order to avoid heap
    // allocation, with the downside that you must make sure that the example_callback2
    // object that the socket holds a pointer to is not destroyed while it's being used.
    udp::socket client(
        udp::opts()
            .port(2240)
            .ipv4_address("127.0.0.1")
            .role(udp::role_e::CLIENT),
        common_opts()
            .receive_callback(&example_callback2));

    // when the client sends, it should trigger the server's callback
    if (client.send("0123456789"))
        thread_printf("Server sent successfully\n");

    // wait a bit for the server's callback to print in the client (you might even
    // see the callback print before the sender print, since the UDP packet transmission
    // and callback should be extremely fast)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // WARNING:
    //  Manual receive() calls like the following will fail when a receive_callback is
    //  being used. Since the receive_callback will exclusively "own" the ability to
    //  receive. This is to prevent race conditions determining who gets to a received
    //  packet first.
    uint8_t rx_data[123] = {};
    if (server.receive(rx_data).status == status_e::DISABLED)
        thread_printf("The manual receive() call was rejected (correctly).\n");

    return 0;
}