/// @brief This example demonstrates how you can use polymorphism to swap between interfaces at compile time
#include "utils/printing.h" // defines thread_printf for thread safe printing

// creating a demo socket that just prints, to use in the demonstration
#include "../include/cpptxrx_threadsafe.h"
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

// NOTE: If you're okay with keeping the dynamic_cast overhead, you can improve compilation
//  times by only exposing the very small abstract-only interface declaration in the header,
//  like this:
//
//    #include "cpptxrx_abstract.h"
//    interface::abstract &get_robot_interface();
//
// Otherwise, to use absolutely NO run-time polymorphism, you can just switch between
// interface types in #if statements, like this:

#ifdef USE_REAL_INTERFACE

using interface_type = udp::socket; // note, you can also return interface::abstract
inline interface_type &get_robot_interface()
{
    static udp::socket instance(udp::socket::opts()
                                    .role(udp::role_e::CLIENT)
                                    .port(1234)
                                    .ipv4_address("127.0.0.1"));
    return instance;
}

#else

using interface_type = printing_socket; // note, you can also return interface::abstract
inline interface_type &get_robot_interface()
{
    static printing_socket instance(interface::open_without_opts);
    return instance;
}

#endif

// This class knows that it's using the type "interface_type", so it does not need any runtime
// polymorphism, it's using the real type, but the interfaces will still have the same API, so
// no code/#ifdef changes are needed at this level:
struct my_robot_actuator
{
    interface_type &to_actuator = get_robot_interface();
    void move_to_angle_in_degrees(double angle)
    {
        thread_printf("Called move_to_angle_in_degrees(%.3f) using %s to send %zu bytes\n", angle, to_actuator.name(), sizeof(angle));
        auto send_status = to_actuator.send(reinterpret_cast<uint8_t *>(&angle), sizeof(angle));
        thread_printf("Send status was: %s\n", send_status.c_str());
    }
};

// You can still define APIs that accept any "interface::abstract" class, but note that if you
// instead use "interface_type", you will remove the dynamic_cast<> overhead that
// the "interface::abstract" approach will incur:
static void this_function_will_work_with_any_interface(interface::abstract &conn)
{
    if (conn.send("stuff"))
        thread_printf("\tthis_function_will_work_with_any_interface() worked!\n");
    else
        thread_printf("\tthis_function_will_work_with_any_interface() failed!\n");
}

int main()
{
    // demonstrates the code working:
    my_robot_actuator().move_to_angle_in_degrees(5.678);
    this_function_will_work_with_any_interface(my_robot_actuator().to_actuator);

    return 0;
}