/// @brief This example demonstrates how you can use polymorphism to swap out
#include "../include/default_udp.h"
#include "utils/printing.h" // defines thread_printf for thread safe printing
#include <memory>

// here we create a fake socket interface for this example that just prints to the terminal when sending and receiving
namespace sim
{
    struct socket : interface::thread_safe<interface::no_opts>
    {
    public:
        IMPORT_CPPTXRX_CTOR_AND_DTOR(socket);
        [[nodiscard]] const char *name() const override { return "sim::socket"; }

    private:
        void process_close() override { transactions.p_close_op->end_op(); }
        void process_open() override { transactions.p_open_op->end_op(); }
        void process_send_receive() override
        {
            if (transactions.p_send_op)
            {
                thread_printf("\tpretending to send %zu bytes\n", transactions.p_send_op->send_size);
                transactions.p_send_op->end_op();
            }
            if (transactions.p_recv_op)
            {
                thread_printf("\tpretending to receive 1 byte\n");
                transactions.p_recv_op->received_data[0]   = 0xAB;
                transactions.p_recv_op->returned_recv_size = 1;
                transactions.p_recv_op->end_op();
            }
        }
        void wake_process() override {} // none of the process_ methods can block, so no need to do anything here
    };
} // namespace sim

// we can switch between interfaces using this global variable, but you could also use env variables, or some other runtime-logic
// mechanism so that you don't have to resort to multiple builds with compile time #if switches, or object linking tricks
bool in_simulation_mode = false;

// this class holds onto an abstract interface object that is used to point to either a "sim::socket" type interface, or a "udp::socket" interface
struct my_robot_actuator
{
    std::unique_ptr<interface::abstract> to_actuator{};

    my_robot_actuator()
    {
        if (in_simulation_mode)
            to_actuator = std::make_unique<sim::socket>(interface::no_opts());
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
}