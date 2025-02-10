//// @file default_udp_raw.h
/// @author Darren V Levine (DarrenVLevine@gmail.com)
/// @brief A simple default implementation of a UDP wrapped non-thread-safe (raw) interface in case you don't want to wrap your own
///
/// @copyright (c) 2024 Darren V Levine. This code is licensed under MIT license (see LICENSE file for details).
///
#ifndef CPPTXRX_DEFAULT_UDP_RAW_H_
#define CPPTXRX_DEFAULT_UDP_RAW_H_

#include "cpptxrx_raw.h"
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

namespace udp
{
    /// @brief UDP socket role type (CLIENT/SERVER)
    enum class role_e
    {
        CLIENT,
        SERVER
    };

    /// @brief UDP socket options
    struct opts
    {
        role_e m_role              = role_e::CLIENT;
        uint16_t m_port            = 0;
        int m_domain               = -1;
        sockaddr_storage m_address = {};
        socklen_t m_address_size   = 0;

        CPPTXRX_OPTS_SETTER(role);
        inline opts &port(uint16_t v)
        {
            m_port = v;
            if (m_domain == AF_INET)
                reinterpret_cast<sockaddr_in &>(m_address).sin_port = htons(m_port);
            else if (m_domain == AF_INET6)
                reinterpret_cast<sockaddr_in6 &>(m_address).sin6_port = htons(m_port);
            return *this;
        }
        inline opts &ipv4_address(const char *input_addr)
        {
            memset(&m_address, 0, sizeof(m_address));
            auto &addr_ref      = reinterpret_cast<sockaddr_in &>(m_address);
            m_domain            = AF_INET;
            addr_ref.sin_family = m_domain;
            addr_ref.sin_port   = htons(m_port);
            if (inet_pton(m_domain, input_addr == nullptr ? "?" : input_addr, &addr_ref.sin_addr.s_addr) == 1)
                m_address_size = sizeof(sockaddr_in);
            else
                m_address_size = 0;
            return *this;
        }
        inline opts &ipv6_address(const char *input_addr)
        {
            memset(&m_address, 0, sizeof(m_address));
            auto &addr_ref       = reinterpret_cast<sockaddr_in6 &>(m_address);
            m_domain             = AF_INET6;
            addr_ref.sin6_family = m_domain;
            addr_ref.sin6_port   = htons(m_port);
            if (inet_pton(m_domain, input_addr == nullptr ? "?" : input_addr, &addr_ref.sin6_addr.s6_addr) == 1)
                m_address_size = sizeof(sockaddr_in6);
            else
                m_address_size = 0;
            return *this;
        }
    };

    struct socket_utilities
    {
        int socket_wake_fd = -1;
        int socket_fd      = -1;

        template <bool threadsafe>
        void construct()
        {
            signal(SIGPIPE, SIG_IGN);

            if constexpr (threadsafe)
            {
                socket_wake_fd = eventfd(0, EFD_NONBLOCK);
                if (socket_wake_fd < 0)
                    raise(SIGSEGV);
            }
        }

        template <bool threadsafe>
        void destruct()
        {
            if (socket_fd != -1 && ::close(socket_fd) == -1)
                raise(SIGSEGV);

            if constexpr (threadsafe)
            {
                if (socket_wake_fd != -1 && ::close(socket_wake_fd) != 0)
                    raise(SIGSEGV);
            }
        }

        bool close_socket(interface::status_e &m_open_status)
        {
            if (socket_fd == -1)
                return true;
            if (::close(socket_fd) == -1)
            {
                m_open_status.set_error_code(static_cast<unsigned int>(errno), "CLOSE_ERR");
                return false;
            }
            m_open_status = interface::status_e::NOT_OPEN;
            socket_fd     = -1;
            return true;
        }

        void wake_process()
        {
            uint64_t val = 1;
            if (::write(socket_wake_fd, &val, sizeof(val)) != sizeof(val))
                raise(SIGSEGV);
        }

        void process_close(interface::transactions_args<opts> &conn)
        {
            if (close_socket(conn.m_open_status))
                conn.transactions.p_close_op->end_op(interface::status_e::SUCCESS);
            else
                conn.transactions.p_close_op->end_op_with_error_code(EIO, "CLOSE_FAILED");
        }

        void process_open(interface::transactions_args<opts> &conn)
        {
            // make sure the socket is closed first
            if (!close_socket(conn.m_open_status))
                conn.transactions.p_open_op->end_op_with_error_code(EPERM, "CLOSE_FAILED_IN_REOPEN");
            else if (conn.m_open_opts.m_port == 0u)
                conn.transactions.p_open_op->end_op_with_error_code(EINVAL, "INVALID_PORT");
            else if (conn.m_open_opts.m_address_size == 0)
                conn.transactions.p_open_op->end_op_with_error_code(EINVAL, "INVALID_ADDR");
            else if (conn.m_open_opts.m_domain != AF_INET && conn.m_open_opts.m_domain != AF_INET6)
                conn.transactions.p_open_op->end_op_with_error_code(EINVAL, "INVALID_DOMAIN");
            else
            {
                socket_fd = ::socket(conn.m_open_opts.m_domain, SOCK_DGRAM, 0);
                if (socket_fd < 0)
                    conn.transactions.p_open_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SOCK_CREATE_FAILURE");
            }

            if (!conn.transactions.p_open_op->is_operating())
                return;

            int option = 1;
            if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1)
            {
                conn.transactions.p_open_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SO_REUSEADDR_FAILURE");
                close_socket(conn.m_open_status);
                return;
            }

            if (conn.m_open_opts.m_role == role_e::SERVER &&
                ::bind(socket_fd, reinterpret_cast<sockaddr *>(&conn.m_open_opts.m_address), conn.m_open_opts.m_address_size) == -1)
            {
                conn.transactions.p_open_op->end_op_with_error_code(static_cast<unsigned int>(errno), "BIND_FAILURE");
                close_socket(conn.m_open_status);
                return;
            }

            conn.transactions.p_open_op->end_op(interface::status_e::SUCCESS);
        }

        template <bool threadsafe>
        void process_send_receive(interface::transactions_args<opts> &conn)
        {
            // convert min_timeout to timeval
            auto min_timeout = conn.transactions.duration_until_timeout({conn.transactions.p_recv_op, conn.transactions.p_send_op});
            auto seconds     = std::chrono::duration_cast<std::chrono::seconds>(min_timeout);
            if (seconds > min_timeout)
                seconds -= std::chrono::seconds{1};
            timeval tv;
            tv.tv_sec  = seconds.count();
            tv.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(min_timeout - seconds).count();

            const bool receiving = conn.transactions.p_recv_op != nullptr;
            const bool sending   = conn.transactions.p_send_op != nullptr;

            fd_set read_fds;
            FD_ZERO(&read_fds);
            if constexpr (threadsafe)
                FD_SET(socket_wake_fd, &read_fds);
            if (receiving)
                FD_SET(socket_fd, &read_fds);

            fd_set write_fds;
            if (sending)
            {
                FD_ZERO(&write_fds);
                FD_SET(socket_fd, &write_fds);
            }

            fd_set except_fds;
            FD_ZERO(&except_fds);
            FD_SET(socket_fd, &except_fds);
            if constexpr (threadsafe)
                FD_SET(socket_wake_fd, &except_fds);

            int maxfd  = socket_fd > socket_wake_fd ? socket_fd : socket_wake_fd;
            int events = ::select(maxfd + 1, &read_fds, sending ? &write_fds : NULL, &except_fds, &tv);

            // check for a timeout
            if (events == 0)
                return;

            // check for a select error
            if (events < 0)
            {
                if (sending)
                    conn.transactions.p_send_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SELECT_ERROR_IN_TX");
                if (receiving)
                    conn.transactions.p_recv_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SELECT_ERROR_IN_RX");
                close_socket(conn.m_open_status);
                return;
            }

            // check for a socket exception
            if (FD_ISSET(socket_fd, &except_fds))
            {
                if (sending)
                    conn.transactions.p_send_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SELECT_SOCK_EXCEPTION_IN_TX");
                if (receiving)
                    conn.transactions.p_recv_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SELECT_SOCK_EXCEPTION_IN_RX");
                close_socket(conn.m_open_status);
                return;
            }

            // check for a eventfd exception
            if constexpr (threadsafe)
                if (FD_ISSET(socket_wake_fd, &except_fds))
                {
                    if (sending)
                        conn.transactions.p_send_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SELECT_EFD_EXCEPTION_IN_TX");
                    if (receiving)
                        conn.transactions.p_recv_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SELECT_EFD_EXCEPTION_IN_RX");
                    close_socket(conn.m_open_status);
                    return;
                }

            // check to see if wake_process() was called
            if constexpr (threadsafe)
                if (FD_ISSET(socket_wake_fd, &read_fds))
                {
                    uint64_t val;
                    if (::read(socket_wake_fd, &val, sizeof(val)) == -1)
                    {
                        if (sending)
                            conn.transactions.p_send_op->end_op_with_error_code(static_cast<unsigned int>(errno), "EFD_READ_ERR_IN_TX");
                        if (receiving)
                            conn.transactions.p_recv_op->end_op_with_error_code(static_cast<unsigned int>(errno), "EFD_READ_ERR_IN_RX");
                        close_socket(conn.m_open_status);
                        return;
                    }
                }

            // check to see if the socket has data to be received
            if (receiving && FD_ISSET(socket_fd, &read_fds))
            {
                auto read_size = ::recvfrom(socket_fd, conn.transactions.p_recv_op->received_data, conn.transactions.p_recv_op->max_receive_size,
                                            0, reinterpret_cast<sockaddr *>(&conn.m_open_opts.m_address), &conn.m_open_opts.m_address_size);
                if (read_size >= 0)
                {
                    conn.transactions.p_recv_op->status        = interface::status_e::SUCCESS;
                    conn.transactions.p_recv_op->received_size = static_cast<size_t>(read_size);
                }
                else
                {
                    conn.transactions.p_recv_op->end_op_with_error_code(static_cast<unsigned int>(errno), "RECVFROM_FAILED");
                    close_socket(conn.m_open_status);
                    return;
                }
            }

            // check to see if the socket is ready for a send
            if (sending && FD_ISSET(socket_fd, &write_fds))
            {
                ssize_t send_size = ::sendto(socket_fd, conn.transactions.p_send_op->send_data, conn.transactions.p_send_op->send_size,
                                             0, reinterpret_cast<sockaddr *>(&conn.m_open_opts.m_address), conn.m_open_opts.m_address_size);
                if (send_size == static_cast<ssize_t>(conn.transactions.p_send_op->send_size))
                    conn.transactions.p_send_op->end_op(interface::status_e::SUCCESS);
                else
                {
                    conn.transactions.p_recv_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SENDTO_FAILED");
                    close_socket(conn.m_open_status);
                    return;
                }
            }
        }
    };

    /// @brief a non thread-safe udp socket
    class socket_raw : public interface::raw<opts>
    {
    public:
        CPPTXRX_IMPORT_CTOR_AND_DTOR(socket_raw);

        [[nodiscard]] virtual const char *name() const final { return "udp::socket_raw"; }
        [[nodiscard]] virtual int id() const final { return 0x0B83; }

    protected:
        friend struct socket_utilities;
        socket_utilities utils = {};

        void construct() final
        {
            utils.construct<false>();
        }
        void destruct() final
        {
            utils.destruct<false>();
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
            utils.process_send_receive<false>(*this);
        }
    };

} // namespace udp

#endif // CPPTXRX_DEFAULT_UDP_RAW_H_