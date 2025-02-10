//// @file default_tcp.h
/// @author Darren V Levine (DarrenVLevine@gmail.com)
/// @brief A simple default implementation of a TCP wrapped thread-safe interface in case you don't want to wrap your own
///
/// @copyright (c) 2024 Darren V Levine. This code is licensed under MIT license (see LICENSE file for details).
///
#ifndef CPPTXRX_DEFAULT_TCP_H_
#define CPPTXRX_DEFAULT_TCP_H_

#include "cpptxrx_threadsafe.h"
#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/poll.h>

namespace tcp
{
    /// @brief TCP socket role type (CLIENT/SERVER)
    enum class role_e
    {
        CLIENT,
        SERVER
    };

    /// @brief TCP socket options
    struct opts
    {
        uint16_t m_port            = 0;
        int m_domain               = -1;
        sockaddr_storage m_address = {};
        socklen_t m_address_size   = 0;

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

    /// @brief a thread-safe tcp socket
    ///
    /// @tparam   role: either "role_e::CLIENT", or "role_e::SERVER". Specified as a template argument instead of a
    ///                 opts argument so that the client doesn't need the overhead of holding onto a unused list of clients,
    ///                 or dynamically allocating memory for a list.
    /// @tparam   max_clients: maximum number of clients to manage. Defaults to 0 if a client, and 100  if a server.
    template <
        role_e role_arg,
        size_t max_clients_arg = role_arg == role_e::CLIENT
                                     ? 0
                                     : 100>
    class socket : public interface::thread_safe<opts>
    {
    public:
        static constexpr role_e role        = role_arg;
        static constexpr size_t max_clients = max_clients_arg;
        static_assert(!(role_arg == role_e::CLIENT && max_clients_arg > 0u), "clients cannot have >0  clients");

        CPPTXRX_IMPORT_CTOR_AND_DTOR(socket);

        [[nodiscard]] virtual const char *name() const final
        {
            if constexpr (role == role_e::CLIENT)
                return "tcp::socket<client>";
            return "tcp::socket<server>";
        }
        [[nodiscard]] virtual int id() const final
        {
            return role == role_e::CLIENT
                       ? 0x6B44
                       : 0x2654;
        }

    protected:
        static constexpr size_t start_of_clients_index = 2;
        static constexpr size_t max_fds                = max_clients + start_of_clients_index;
        struct pollfd fds[max_fds]                     = {};
        size_t fds_size                                = start_of_clients_index;
        static constexpr size_t socket_wake_index      = 0;
        static constexpr size_t socket_index           = 1;

        bool add_client(int new_fd)
        {
            if (fds_size >= max_fds)
                return false;
            fds[fds_size++].fd = new_fd;
            return true;
        }
        void prune_closed_clients()
        {
            if constexpr (max_clients > 0u)
            {
                size_t last_index = fds_size - 1u;
                while (fds_size > start_of_clients_index)
                {
                    if (fds[last_index].fd == -1)
                    {
                        --fds_size;
                        --last_index;
                        continue;
                    }

                    size_t fd_index = last_index;
                    while (--fd_index > 1)
                    {
                        if (fds[fd_index].fd == -1)
                        {
                            fds[fd_index].fd       = fds[last_index].fd;
                            fds[fd_index].events   = fds[last_index].events;
                            fds[last_index].fd     = -1;
                            fds[last_index].events = 0;
                            --fds_size;
                            --last_index;
                        }
                    }
                    break;
                }
            }
        }
        void construct() final
        {
            signal(SIGPIPE, SIG_IGN);

            memset(fds, 0, sizeof(fds));
            for (size_t fd_index = 0; fd_index < fds_size; fd_index++)
                fds[fd_index].fd = -1;

            fds[socket_wake_index].events = POLLIN;
            fds[socket_index].events      = POLLIN;

            fds[socket_wake_index].fd = eventfd(0, EFD_NONBLOCK);
            if (fds[socket_wake_index].fd < 0)
                raise(SIGSEGV);
        }
        void destruct() final
        {
            close_all_sockets();
            close_one_socket(socket_wake_index, needs_shutdown_e::NO);
        }
        enum class needs_shutdown_e
        {
            YES,
            NO
        };
        bool close_one_socket(size_t index, needs_shutdown_e call_shutdown = needs_shutdown_e::YES)
        {
            if (index >= max_fds)
            {
                m_open_status.set_error_code(static_cast<unsigned int>(EIO), "INDEX_INVALID");
                return true;
            }

            auto &fd_info = fds[index];
            if (fd_info.fd == -1)
                return true;

            if (call_shutdown == needs_shutdown_e::YES && ::shutdown(fd_info.fd, SHUT_RDWR) == -1)
                m_open_status.set_error_code(static_cast<unsigned int>(errno), "SHUTDOWN_SHUT_RDWR_ERR");

            if (::close(fd_info.fd) == -1)
                m_open_status.set_error_code(static_cast<unsigned int>(errno), "CLOSE_ERR");

            fd_info.fd     = -1;
            fd_info.events = 0;
            return true;
        }
        bool close_all_sockets(needs_shutdown_e call_shutdown = needs_shutdown_e::YES)
        {
            transactions.idle_in_send_recv = false; // don't bother idling if the socket isn't open
            bool all_closed                = true;
            for (size_t fd_index = start_of_clients_index; fd_index < fds_size; fd_index++)
                all_closed &= close_one_socket(fd_index, call_shutdown);
            all_closed &= close_one_socket(socket_index, call_shutdown);
            if (all_closed)
                m_open_status = interface::status_e::NOT_OPEN;
            else
                m_open_status.set_error_code(EIO, "CLOSE_FAILED");
            return all_closed;
        }

        void process_close() final
        {
            if (close_all_sockets())
                transactions.p_close_op->end_op(interface::status_e::SUCCESS);
            else
                transactions.p_close_op->end_op_with_error_code(EIO, "CLOSE_FAILED");
        }

        void process_open() final
        {
            // make sure the socket is closed first
            if (!close_all_sockets())
                transactions.p_open_op->end_op_with_error_code(EPERM, "CLOSE_FAILED_IN_REOPEN");
            else if (m_open_opts.m_port == 0u)
                transactions.p_open_op->end_op_with_error_code(EINVAL, "INVALID_PORT");
            else if (m_open_opts.m_address_size == 0)
                transactions.p_open_op->end_op_with_error_code(EINVAL, "INVALID_ADDR");
            else if (m_open_opts.m_domain != AF_INET && m_open_opts.m_domain != AF_INET6)
                transactions.p_open_op->end_op_with_error_code(EINVAL, "INVALID_DOMAIN");
            else
            {
                fds[socket_index].fd = ::socket(m_open_opts.m_domain, SOCK_STREAM, 0);
                if (fds[socket_index].fd < 0)
                    transactions.p_open_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SOCK_CREATE_FAILURE");
            }

            if (!transactions.p_open_op->is_operating())
                return;

            if constexpr (role == role_e::SERVER)
            {
                int option = 1;
                if (setsockopt(fds[socket_index].fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1)
                {
                    transactions.p_open_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SO_REUSEADDR_FAILURE");
                    close_all_sockets(needs_shutdown_e::NO);
                    return;
                }

                if (::bind(fds[socket_index].fd, reinterpret_cast<sockaddr *>(&m_open_opts.m_address), m_open_opts.m_address_size) == -1)
                {
                    transactions.p_open_op->end_op_with_error_code(static_cast<unsigned int>(errno), "BIND_FAILURE");
                    close_all_sockets(needs_shutdown_e::NO);
                    return;
                }

                if (::listen(fds[socket_index].fd, max_clients) < 0)
                {
                    transactions.p_open_op->end_op_with_error_code(static_cast<unsigned int>(errno), "LISTEN_FAILURE");
                    close_all_sockets(needs_shutdown_e::YES);
                    return;
                }
            }
            else // if CLIENT:
            {
                if (::connect(fds[socket_index].fd, reinterpret_cast<sockaddr *>(&m_open_opts.m_address), m_open_opts.m_address_size) == -1)
                {
                    transactions.p_open_op->end_op_with_error_code(static_cast<unsigned int>(errno), "CONNECT_FAILURE");
                    close_all_sockets(needs_shutdown_e::NO);
                    return;
                }
            }

            // enabling idle keeps calling process_send_receive while the socket is open
            transactions.idle_in_send_recv = true;

            transactions.p_open_op->end_op(interface::status_e::SUCCESS);
        }

        inline bool accept_and_clear_event(size_t fd_index, int option)
        {
            if (fds[fd_index].revents & option)
            {
                fds[fd_index].revents &= ~option;
                return true;
            }
            return false;
        }

        bool check_if_some_clients_still_need_to_send()
        {
            for (size_t i = start_of_clients_index; i < fds_size; i++)
                if ((fds[i].events & POLLOUT) != 0)
                    return true;
            return false;
        }

        ssize_t send_all(int fd, const uint8_t *data, size_t size)
        {
            size_t len_remaining = size;
            while (len_remaining > 0)
            {
                ssize_t r = ::write(fd, data, len_remaining);
                if (r < 0)
                {
                    if (errno == EINTR || errno == EAGAIN)
                        continue;
                    else
                        return r;
                }
                len_remaining -= r;
                data += r;
            }
            return size;
        }

        bool close_with_error_and_check_if_fatal(size_t fd_index, needs_shutdown_e call_shutdown, int err_code, const char *&&error_msg)
        {
            close_one_socket(fd_index, call_shutdown);
            if (fd_index == socket_index)
            {
                close_all_sockets();
                m_open_status.set_error_code(static_cast<unsigned int>(err_code), std::forward<const char *&&>(error_msg));
                if (transactions.p_send_op != nullptr && transactions.p_send_op->is_operating())
                    transactions.p_send_op->end_op_with_error_code(static_cast<unsigned int>(err_code), "SEND_FAILED_SOCK_CLOSED");
                if (transactions.p_recv_op != nullptr && transactions.p_recv_op->is_operating())
                    transactions.p_recv_op->end_op_with_error_code(static_cast<unsigned int>(err_code), "RECV_FAILED_SOCK_CLOSED");
                return true;
            }
            return false;
        }

        size_t round_robin_client_offset = 0;
        void process_send_receive_and_idle()
        {
            // convert min_timeout to an int in milliseconds (used by "poll")
            auto min_timeout = transactions.duration_until_timeout({transactions.p_recv_op, transactions.p_send_op});
            if (min_timeout < std::chrono::nanoseconds::max() - std::chrono::microseconds(999))
                min_timeout += std::chrono::microseconds(999); // round up to nearest ms
            auto i64_timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(min_timeout).count();
            int timeout_ms      = i64_timeout_ms > std::numeric_limits<int>::max()
                                      ? std::numeric_limits<int>::max()
                                      : static_cast<int>(i64_timeout_ms);

            const bool receiving       = transactions.p_recv_op != nullptr;
            const bool sending         = transactions.p_send_op != nullptr;
            const int requested_events = (receiving * POLLIN) | (sending * POLLOUT);
            if constexpr (role == role_e::CLIENT)
                fds[socket_index].events = requested_events;
            else
            {
                // if we're not starting a new send, but instead continuing an old one, we need to preserve
                // the prior sending event flags in order to know which clients have already been sent on
                if (sending && transactions.p_send_op->status != interface::status_e::START_NEW_OP)
                {
                    for (size_t fd_index = start_of_clients_index; fd_index < fds_size; fd_index++)
                    {
                        bool still_needs_sending = fds[fd_index].events & POLLOUT;
                        fds[fd_index].events     = (receiving * POLLIN) | (still_needs_sending * POLLOUT);
                    }
                }
                else
                {
                    for (size_t fd_index = start_of_clients_index; fd_index < fds_size; fd_index++)
                        fds[fd_index].events = requested_events;
                }

                // if only one specific send channel is selected, then turn off sending on any channel that isn't that fd number
                if (sending && transactions.p_send_op->send_channel != interface::default_unset_channel)
                {
                    for (size_t fd_index = start_of_clients_index; fd_index < fds_size; fd_index++)
                        if (fds[fd_index].fd != transactions.p_send_op->send_channel)
                            fds[fd_index].events = (receiving * POLLIN);
                }
            }

            // wait here for a wake event or any of the requested operations
            int num_events = ::poll(fds, fds_size, timeout_ms);

            // check for a timeout
            if (num_events == 0)
                return;

            // check for a poll error
            if (num_events < 0)
            {
                if (sending)
                    transactions.p_send_op->end_op_with_error_code(static_cast<unsigned int>(errno), "POLL_ERROR_IN_TX");
                if (receiving)
                    transactions.p_recv_op->end_op_with_error_code(static_cast<unsigned int>(errno), "POLL_ERROR_IN_RX");
                close_all_sockets();
                return;
            }

            // need to save a copy of the size, because it may change in the loop if new clients
            // are added, and the current list of poll revents should be serviced first
            size_t original_size = fds_size;

            // nee to update the client index mapping before looping, so that changes to the round_robin_client_offset don't effect the result in-loop
            size_t original_num_clients = original_size - start_of_clients_index;
            if (++round_robin_client_offset >= original_num_clients)
                round_robin_client_offset = 0u;
            size_t used_round_robin_client_offset = round_robin_client_offset;

            for (size_t fd_index_raw = 0u; fd_index_raw < original_size; fd_index_raw++)
            {
                // map the raw fd index into an index that rotates through clients as they get used, in order to more fairly queue receives and sends
                size_t fd_index = fd_index_raw;
                if (fd_index_raw >= start_of_clients_index && original_num_clients > 1)
                    fd_index = start_of_clients_index + ((fd_index - start_of_clients_index + used_round_robin_client_offset) % original_num_clients);

                // no events
                if (fds[fd_index].revents == 0)
                    continue;

                // break out early if no more events need handling
                if (--num_events == -1)
                    break;

                // Invalid request: fd not open (only returned in revents; ignored in events).
                if (accept_and_clear_event(fd_index, POLLNVAL))
                {
                    if (close_with_error_and_check_if_fatal(fd_index, needs_shutdown_e::NO, EIO, "INVALID_FD"))
                        return;
                    continue;
                }

                // POLLHUP: Hang up (only returned in revents; ignored in events). Note that when reading from a channel such as a pipe or a
                //     stream socket, this event merely indicates that the peer closed its end of the channel.  Subsequent reads from the
                //     channel will return 0 (end of file) only after all outstanding data in the channel has been consumed.
                // POLLRDHUP: Stream socket peer closed connection, or shut down writing half of connection.  The _GNU_SOURCE feature test macro
                //     must be defined (before including any header files) in order to obtain this definition.
                if (accept_and_clear_event(fd_index, POLLHUP | POLLRDHUP))
                {
                    if (close_with_error_and_check_if_fatal(fd_index, needs_shutdown_e::NO, EIO, (role == role_e::CLIENT) ? "SERVER_CLOSED_CONN" : "CLIENT_CLOSED_CONN"))
                        return;
                    continue;
                }

                // There is urgent priority data to read. (can be ignored - assumed to be used by the sender as a signaling/waking mechanism)
                accept_and_clear_event(fd_index, POLLPRI);

                // There is normal data to read.
                if (accept_and_clear_event(fd_index, POLLIN))
                {
                    if (fd_index == socket_wake_index)
                    {
                        uint64_t val;
                        if (::read(fds[socket_wake_index].fd, &val, sizeof(val)) == -1)
                        {
                            auto err_val = static_cast<unsigned int>(errno);
                            if (sending)
                                transactions.p_send_op->end_op_with_error_code(err_val, "EFD_READ_ERR_IN_TX");
                            if (receiving)
                                transactions.p_recv_op->end_op_with_error_code(err_val, "EFD_READ_ERR_IN_RX");
                            close_all_sockets();
                            m_open_status.set_error_code(err_val, "EFD_READ_ERR");
                            return;
                        }
                    }
                    else if (role == role_e::SERVER && fd_index == socket_index)
                    {
                        int new_client_fd = ::accept(fds[fd_index].fd, NULL, NULL);
                        if (new_client_fd >= 0)
                        {
                            if (!add_client(new_client_fd))
                            {
                                // reject the client, since it won't fit
                                if (::shutdown(new_client_fd, SHUT_RDWR) == -1)
                                    m_open_status.set_error_code(static_cast<unsigned int>(errno), "SHUTDOWN_SHUT_RDWR_ERR");
                                if (::close(new_client_fd) == -1)
                                    m_open_status.set_error_code(static_cast<unsigned int>(errno), "CLOSE_ERR");
                            }
                        }
                        else if (errno != EAGAIN)
                        {
                            close_all_sockets();
                            m_open_status.set_error_code(static_cast<unsigned int>(errno), "SERVER_ACCEPT_FAILED");
                            return;
                        }
                    }
                    else
                    {
                        if (!receiving)
                        {
                            close_all_sockets();
                            m_open_status.set_error_code(static_cast<unsigned int>(EIO), "GOT_RECV_WITHOUT_RECEIVING");
                            return;
                        }

                        // don't allow further operations to override existing finished ones
                        if (transactions.p_recv_op->is_operating())
                        {
                            ssize_t read_size = ::read(fds[fd_index].fd, transactions.p_recv_op->received_data, transactions.p_recv_op->max_receive_size);
                            if (read_size > 0)
                            {
                                transactions.p_recv_op->received_size    = static_cast<size_t>(read_size);
                                transactions.p_recv_op->received_channel = fds[fd_index].fd;
                                transactions.p_recv_op->end_op(interface::status_e::SUCCESS);
                            }
                            else if (read_size < 0 && errno != EAGAIN)
                            {
                                transactions.p_recv_op->end_op_with_error_code(static_cast<unsigned int>(errno), "RECVFROM_FAILED");
                                if (close_with_error_and_check_if_fatal(fd_index, needs_shutdown_e::YES, errno, "RECVFROM_FAILED"))
                                    return;
                                continue;
                            }
                            else if (read_size == 0)
                            {
                                if (close_with_error_and_check_if_fatal(fd_index, needs_shutdown_e::YES, errno, "RECV_FAILED_SOCK_CLOSED"))
                                    return;
                                continue;
                            }
                        }
                    }
                }

                // Writing is now possible, though a write larger than the available space in a socket or pipe will still block
                // (unless O_NONBLOCK is set)
                if (accept_and_clear_event(fd_index, POLLOUT))
                {
                    if (!sending)
                    {
                        close_all_sockets();
                        m_open_status.set_error_code(static_cast<unsigned int>(EIO), "GOT_SEND_WITHOUT_SENDING");
                        return;
                    }

                    // don't allow further operations to override existing finished ones
                    if (transactions.p_send_op->is_operating())
                    {
                        ssize_t send_size = send_all(fds[fd_index].fd, transactions.p_send_op->send_data, transactions.p_send_op->send_size);
                        if (send_size == static_cast<ssize_t>(transactions.p_send_op->send_size))
                        {
                            // just ending client sends, the server send completion is handled later by
                            // checking all the "fds[fd_index].events" POLLOUT flags
                            if constexpr (role == role_e::CLIENT)
                                transactions.p_send_op->end_op(interface::status_e::SUCCESS);
                            else
                                fds[fd_index].events &= ~POLLOUT; // disallow sending on this fd until the next send op
                        }
                        else if (errno != EAGAIN)
                        {
                            transactions.p_send_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SENDTO_FAILED");
                            if (close_with_error_and_check_if_fatal(fd_index, needs_shutdown_e::YES, errno, "SENDTO_FAILED"))
                                return;
                            continue;
                        }
                    }
                }

                // Error condition (only returned in revents; ignored in events).  This bit is also set for a file descriptor
                // referring to the write end of a pipe when the read end has been closed.
                if (accept_and_clear_event(fd_index, POLLERR))
                {
                    if (close_with_error_and_check_if_fatal(fd_index, needs_shutdown_e::YES, errno, "POLLERR"))
                        return;
                    continue;
                }

                // Check if the system your running on is emitting other event flags that aren't getting handled
                if (fds[fd_index].revents != 0)
                {
                    if (close_with_error_and_check_if_fatal(fd_index, needs_shutdown_e::YES, fds[fd_index].revents, "UNHANDLED_EVENTS"))
                        return;
                    continue;
                }
            }
        }

        void process_send_receive() final
        {
            // spend time servicing the active connections and operations
            process_send_receive_and_idle();

            // if any operation removed clients, remove them
            prune_closed_clients();

            // check if the server send can be considered complete, after pruning:
            if constexpr (role == role_e::SERVER)
            {
                const bool send_is_finished =
                    transactions.p_send_op != nullptr &&         // if there is a send op
                    transactions.p_send_op->is_operating() &&    // if the send op isn't already complete
                    !check_if_some_clients_still_need_to_send(); // if all the existing clients have already sent

                if (send_is_finished)
                {
                    if (fds_size > start_of_clients_index) // if there are clients to send over)
                        transactions.p_send_op->end_op(interface::status_e::SUCCESS);
                    else
                        transactions.p_send_op->end_op_with_error_code(EIO, "SEND_FAILED_NO_CLIENTS");
                }
            }
        }
        void wake_process() final
        {
            uint64_t val = 1;
            if (::write(fds[socket_wake_index].fd, &val, sizeof(val)) != sizeof(val))
                raise(SIGSEGV);
        }
    };
} // namespace tcp
#endif // CPPTXRX_DEFAULT_TCP_H_