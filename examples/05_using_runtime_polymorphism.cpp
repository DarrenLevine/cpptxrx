/// @brief This example demonstrates how you can use polymorphism to swap between interfaces at runtime
#include "../include/default_udp.h"
#include "utils/printing.h" // defines thread_printf for thread safe printing

// here we create a fake socket interface for this example that just prints to the terminal when sending and receiving
struct printing_socket : interface::thread_safe<interface::no_opts>
{
    CPPTXRX_CREATE_EMPTY_DEMO_METHODS(printing_socket);
    void process_send(interface::send_op &op)
    {
        thread_printf("\tpretending to send %zu bytes\n", op.send_size);
        op.end_op();
    }
    void process_recv(interface::recv_op &op)
    {
        thread_printf("\tpretending to receive 1 byte\n");
        op.received_data[0] = 0xAB;
        op.received_size    = 1;
        op.end_op();
    }
};

// we can switch between interfaces using this global variable, but you could also use env variables, or some other runtime-logic
// mechanism so that you don't have to resort to multiple builds with compile time #if switches, or object linking tricks
bool in_simulation_mode = false;

// this class holds onto an abstract interface object that is used to point to either a "printing_socket" type interface, or a "udp::socket" interface
struct my_robot_actuator
{
    std::unique_ptr<interface::abstract> to_actuator{};

    my_robot_actuator()
    {
        if (in_simulation_mode)
            to_actuator = std::make_unique<printing_socket>(interface::no_opts());
        else
            to_actuator = std::make_unique<udp::socket>(
                udp::socket::opts()
                    .role(udp::role_e::CLIENT)
                    .port(1234)
                    .ipv4_address("127.0.0.1"));
    }

    void move_to_angle_in_degrees(double angle)
    {
        thread_printf("Called move_to_angle_in_degrees(%.3f) using %s to send %zu bytes\n", angle, to_actuator->name(), sizeof(angle));
        auto send_status = to_actuator->send(reinterpret_cast<uint8_t *>(&angle), sizeof(angle));
        thread_printf("Send status was: %s\n", send_status.c_str());
    }
};

// you can also define APIs that accept any abstract interface
static void this_function_will_work_with_any_interface(interface::abstract &conn)
{
    if (conn.send("stuff"))
        thread_printf("\tthis_function_will_work_with_any_interface() worked!\n");
    else
        thread_printf("\tthis_function_will_work_with_any_interface() failed!\n");
}

int main()
{
    // we can now switch between different data transport mechanisms, even though from the perspective
    // of our "move_to_angle_in_degrees" code, it's always sending information on one interface.
    // This is especially handy if your application requires different transport mechanisms depending on detected platform settings.

    thread_printf("\nSwitching to HW mode\n");
    in_simulation_mode = false;
    my_robot_actuator().move_to_angle_in_degrees(1.234);

    thread_printf("\nSwitching to SIM mode\n");
    in_simulation_mode = true;
    my_robot_actuator().move_to_angle_in_degrees(5.678);

    thread_printf("\nDemonstrating using an API that accepts generic/abstract interfaces:\n");
    this_function_will_work_with_any_interface(*my_robot_actuator().to_actuator);

    return 0;
}