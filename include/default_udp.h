//// @file default_udp_raw.h
/// @author Darren V Levine (DarrenVLevine@gmail.com)
/// @brief A simple default implementation of a UDP wrapped thread-safe interface in case you don't want to wrap your own
///
/// @copyright (c) 2024 Darren V Levine. This code is licensed under MIT license (see LICENSE file for details).
///
#ifndef CPPTXRX_UDP_H_
#define CPPTXRX_UDP_H_

#include "cpptxrx_threadsafe.h"
#include "default_udp_raw.h"

namespace udp
{
    /// @brief a thread-safe udp socket
    class socket : public interface::thread_safe<opts>
    {
    public:
        IMPORT_CPPTXRX_CTOR_AND_DTOR(socket);

        [[nodiscard]] virtual const char *name() const override { return "udp::socket"; }
        [[nodiscard]] virtual int id() const override { return 0x4208; }

    protected:
        friend struct socket_utilities;
        socket_utilities utils = {};

        void construct() override
        {
            utils.construct<true>();
        }
        void destruct() override
        {
            utils.destruct<true>();
        }
        void process_close() override
        {
            utils.process_close(*this);
        }
        void process_open() override
        {
            utils.process_open(*this);
        }
        void process_send_receive() override
        {
            utils.process_send_receive<true>(*this);
        }
        void wake_process() override
        {
            utils.wake_process();
        }
    };
} // namespace udp
#endif // CPPTXRX_UDP_H_