//// @file default_udp.h
/// @author Darren V Levine (DarrenVLevine@gmail.com)
/// @brief A simple default implementation of a UDP wrapped thread-safe interface in case you don't want to wrap your own
///
/// @copyright (c) 2024 Darren V Levine. This code is licensed under MIT license (see LICENSE file for details).
///
#ifndef CPPTXRX_DEFAULT_UDP_H_
#define CPPTXRX_DEFAULT_UDP_H_

#include "cpptxrx_threadsafe.h"
#include "default_udp_raw.h"

namespace udp
{
    /// @brief a thread-safe udp socket
    class socket : public interface::thread_safe<opts>
    {
    public:
        CPPTXRX_IMPORT_CTOR_AND_DTOR(socket);

        [[nodiscard]] virtual const char *name() const final { return "udp::socket"; }
        [[nodiscard]] virtual int id() const final { return 0x4208; }

    protected:
        friend struct socket_utilities;
        socket_utilities utils = {};

        void construct() final
        {
            utils.construct<true>();
        }
        void destruct() final
        {
            utils.destruct<true>();
        }
        void process_close() final
        {
            utils.process_close(*this);
        }
        void process_open() final
        {
            utils.process_open(*this);
        }
        void process_send_receive() final
        {
            utils.process_send_receive<true>(*this);
        }
        void wake_process() final
        {
            utils.wake_process();
        }
    };
} // namespace udp
#endif // CPPTXRX_DEFAULT_UDP_H_