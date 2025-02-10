/// @brief This is a working example of how to create a interface wrapper using CppTxRx, specifically for a UDP socket.

// headers needed for udp sockets
#include <arpa/inet.h>
#include <cstring>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

// Step 1) include either cpptxrx_threadsafe.h (to make a threadsafe interface) or cpptxrx_raw.h (to create a non threadsafe interface)
#include "../include/cpptxrx_threadsafe.h"

// optionally, to differentiate new interface types its a good to place them in a namespace
namespace udp
{
    enum class role_e
    {
        CLIENT,
        SERVER
    };

    // Step 2) define an "opts" which should define your connection details as you want them passed into an "open()" operations
    struct opts
    {
        role_e m_role              = role_e::CLIENT;
        uint16_t m_port            = 0;
        int m_domain               = -1;
        sockaddr_storage m_address = {};
        socklen_t m_address_size   = 0;

        // it's a good idea to follow the "named parameter idiom" when creating setter methods, to enable method chaining
        // see this link for an explanation: https://isocpp.org/wiki/faq/ctors#named-parameter-idiom
        // you can also use the CPPTXRX_OPTS_SETTER macro to apply the pattern for you, if your member variable is the
        // same name as your setter method, but with a "m_" prefix. Like this:
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

    // Step 3) inherit from either "interface::thread_safe<opts>" to create a thread safe interface
    // or "interface::raw<opts>" for a non-threadsafe interface
    // and fill out the template arguments (note that the timeouts are optional, and just defaults if the operations do not specify any)
    class socket : public interface::thread_safe<
                       opts, // (REQUIRED) options to use when calling "open"
                       interface::timeouts<
                           std::chrono::nanoseconds(std::chrono::seconds(30)).count(), // (OPTIONAL) default recv timeout in ns
                           std::chrono::nanoseconds(std::chrono::seconds(1)).count(),  // (OPTIONAL) default send timeout in ns
                           std::chrono::nanoseconds(std::chrono::seconds(1)).count(),  // (OPTIONAL) default open timeout in ns
                           std::chrono::nanoseconds(std::chrono::seconds(1)).count()>> // (OPTIONAL) default close timeout in ns
    {
    public:
        // Step 4) some of the necessary constructor/destructor functions must be imported
        CPPTXRX_IMPORT_CTOR_AND_DTOR(socket);

        // optionally, feel free to name and/or id your new interface:
        [[nodiscard]] virtual const char *name() const final { return "udp::socket"; }
        [[nodiscard]] virtual int id() const final { return 0x4208; }

    private:
        // Step 5) override the following methods with your implementation
        //      void construct() override;                  (OPTIONAL)
        //      void destruct() override;                   (OPTIONAL)
        //      void process_close() override;              (REQUIRED)
        //      void process_open() override;               (REQUIRED)
        //      void process_send_receive() override;       (REQUIRED)
        //      void wake_process() override;               (OPTIONAL for raw, REQUIRED for threadsafe)

        int socket_wake_fd = -1;
        int socket_fd      = -1;

        void construct() final
        {
            signal(SIGPIPE, SIG_IGN);

            socket_wake_fd = eventfd(0, EFD_NONBLOCK);
            if (socket_wake_fd < 0)
                raise(SIGSEGV);
        }

        void destruct() final
        {
            if (socket_fd != -1 && ::close(socket_fd) == -1)
                raise(SIGSEGV);

            if (socket_wake_fd != -1 && ::close(socket_wake_fd) != 0)
                raise(SIGSEGV);
        }

        bool close_socket()
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

        // Define how to wake up your "process_" methods if they're waiting.
        // This is needed so that any existing "process_" calls can wake and exit the function if they're
        // blocking, so that new operations can be accepted/added for the next time the "process_" functions
        // are called. Allowing "process_" methods to block indefinitely unless there's something new to do.
        // WARNING!: wake_process is the only "overridden" method that can be called from other threads.
        // WARNING!: The wake signal must be sticky (like a eventfd object), since there's no guarantee that wake_process
        //       will be called precisely when your process_ method is performing a block or reading the wake signal.
        void wake_process() final
        {
            uint64_t val = 1;
            if (::write(socket_wake_fd, &val, sizeof(val)) != sizeof(val))
                raise(SIGSEGV);
        }

        void process_close() final
        {
            if (close_socket())
                transactions.p_close_op->end_op(interface::status_e::SUCCESS);
            else
                transactions.p_close_op->end_op_with_error_code(EIO, "CLOSE_FAILED");
        }

        void process_open() final
        {
            // make sure the socket is closed first
            if (!close_socket())
                transactions.p_open_op->end_op_with_error_code(EPERM, "CLOSE_FAILED_IN_REOPEN");
            else if (m_open_opts.m_port == 0u)
                transactions.p_open_op->end_op_with_error_code(EINVAL, "INVALID_PORT");
            else if (m_open_opts.m_address_size == 0)
                transactions.p_open_op->end_op_with_error_code(EINVAL, "INVALID_ADDR");
            else if (m_open_opts.m_domain != AF_INET && m_open_opts.m_domain != AF_INET6)
                transactions.p_open_op->end_op_with_error_code(EINVAL, "INVALID_DOMAIN");
            else
            {
                socket_fd = ::socket(m_open_opts.m_domain, SOCK_DGRAM, 0);
                if (socket_fd < 0)
                    transactions.p_open_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SOCK_CREATE_FAILURE");
            }

            if (!transactions.p_open_op->is_operating())
                return;

            int option = 1;
            if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1)
            {
                transactions.p_open_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SO_REUSEADDR_FAILURE");
                close_socket();
                return;
            }

            if (m_open_opts.m_role == role_e::SERVER &&
                ::bind(socket_fd, reinterpret_cast<sockaddr *>(&m_open_opts.m_address), m_open_opts.m_address_size) == -1)
            {
                transactions.p_open_op->end_op_with_error_code(static_cast<unsigned int>(errno), "BIND_FAILURE");
                close_socket();
                return;
            }

            transactions.p_open_op->end_op(interface::status_e::SUCCESS);
        }

        void process_send_receive() final
        {
            // convert min_timeout to timeval
            auto min_timeout = transactions.duration_until_timeout({transactions.p_recv_op, transactions.p_send_op});
            auto seconds     = std::chrono::duration_cast<std::chrono::seconds>(min_timeout);
            if (seconds > min_timeout)
                seconds -= std::chrono::seconds{1};
            timeval tv;
            tv.tv_sec  = seconds.count();
            tv.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(min_timeout - seconds).count();

            const bool receiving = transactions.p_recv_op != nullptr;
            const bool sending   = transactions.p_send_op != nullptr;

            fd_set read_fds;
            FD_ZERO(&read_fds);
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
                    transactions.p_send_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SELECT_ERROR_IN_TX");
                if (receiving)
                    transactions.p_recv_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SELECT_ERROR_IN_RX");
                close_socket();
                return;
            }

            // check for a socket exception
            if (FD_ISSET(socket_fd, &except_fds))
            {
                if (sending)
                    transactions.p_send_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SELECT_SOCK_EXCEPTION_IN_TX");
                if (receiving)
                    transactions.p_recv_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SELECT_SOCK_EXCEPTION_IN_RX");
                close_socket();
                return;
            }

            // check for a eventfd exception
            if (FD_ISSET(socket_wake_fd, &except_fds))
            {
                if (sending)
                    transactions.p_send_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SELECT_EFD_EXCEPTION_IN_TX");
                if (receiving)
                    transactions.p_recv_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SELECT_EFD_EXCEPTION_IN_RX");
                close_socket();
                return;
            }

            // check to see if wake_process() was called
            if (FD_ISSET(socket_wake_fd, &read_fds))
            {
                uint64_t val;
                if (::read(socket_wake_fd, &val, sizeof(val)) == -1)
                {
                    if (sending)
                        transactions.p_send_op->end_op_with_error_code(static_cast<unsigned int>(errno), "EFD_READ_ERR_IN_TX");
                    if (receiving)
                        transactions.p_recv_op->end_op_with_error_code(static_cast<unsigned int>(errno), "EFD_READ_ERR_IN_RX");
                    close_socket();
                    return;
                }
            }

            // check to see if the socket has data to be received
            if (receiving && FD_ISSET(socket_fd, &read_fds))
            {
                auto read_size = ::recvfrom(socket_fd, transactions.p_recv_op->received_data, transactions.p_recv_op->max_receive_size,
                                            0, reinterpret_cast<sockaddr *>(&m_open_opts.m_address), &m_open_opts.m_address_size);
                if (read_size >= 0)
                {
                    transactions.p_recv_op->status           = interface::status_e::SUCCESS;
                    transactions.p_recv_op->received_size    = static_cast<size_t>(read_size);
                    transactions.p_recv_op->received_channel = socket_fd;
                }
                else
                {
                    transactions.p_recv_op->end_op_with_error_code(static_cast<unsigned int>(errno), "RECVFROM_FAILED");
                    close_socket();
                    return;
                }
            }

            // check to see if the socket is ready for a send
            if (sending && FD_ISSET(socket_fd, &write_fds))
            {
                ssize_t send_size = ::sendto(socket_fd, transactions.p_send_op->send_data, transactions.p_send_op->send_size,
                                             0, reinterpret_cast<sockaddr *>(&m_open_opts.m_address), m_open_opts.m_address_size);
                if (send_size == static_cast<ssize_t>(transactions.p_send_op->send_size))
                    transactions.p_send_op->end_op(interface::status_e::SUCCESS);
                else
                {
                    transactions.p_recv_op->end_op_with_error_code(static_cast<unsigned int>(errno), "SENDTO_FAILED");
                    close_socket();
                    return;
                }
            }
        }
    };
} // namespace udp

#include "utils/printing.h" // defines thread_printf for thread safe printing

int main()
{
    // How to construct AND open the connection simultaneously:
    udp::socket server(udp::socket::opts()
                           .role(udp::role_e::SERVER)
                           .port(1234)
                           .ipv4_address("127.0.0.1"),

                       // you can also optionally specify a timeout or other common options,
                       // like attaching receive_callback functions to receive automatically
                       interface::common_opts()
                           .open_timeout(std::chrono::seconds(10)));

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
        thread_printf("server open error: %s (%s)\n",
                      server.open_status().c_str(),
                      server.open_status().error_c_str());

    // A interfaces can also be created without opening on construction
    udp::socket client;

    // We expect the connection to not be open yet
    thread_printf("client not open yet (%s)\n", client.open_status().c_str());

    // Call open with any open arguments you want
    auto status = client.open(udp::socket::opts()
                                  .role(udp::role_e::CLIENT)
                                  .port(1234)
                                  .ipv4_address("127.0.0.1"));

    // Since it wasn't opened in the constructor, we can check the return status directly instead of
    // using ".open_status()"
    thread_printf("client is now open (%s)\n", status.c_str());

    // A interface can be closed as easily
    status = client.close();
    thread_printf("Close operation status = %s, Current client status = %s\n", status.c_str(), client.open_status().c_str());

    // Now that there are default options are stored, we can call reopen the connection using the last
    // arguments at any time or specify new arguments.
    // Note: In this instance, .open() would also work. The difference between ".open()" and ".reopen()"
    //       is that if ".open()" is called while the interface is already open it will fail, but if
    //       ".reopen()" is called while the interface is already open, it will first be closed.
    status = client.reopen();

    // and we can double check what open arguments were used, even if the open operation modified them
    udp::socket::opts open_args;
    if (client.get_open_args(open_args))
        thread_printf("client is now open (%s) using old arguments (port=%i)\n",
                      status.c_str(), int(open_args.m_port));

    // now, to demonstrate the thread safety, we'll send and receive simultaneously multiple times from
    // multiple threads:
    {
        interface::raii_thread thread1(
            [&]()
            {
                const uint8_t tx_data[] = "hello 1!";
                auto send_status        = client.send(tx_data);
                thread_printf("client sent: \"%s\" (size=%zu bytes, status=%s)\n",
                              tx_data, sizeof(tx_data), send_status.c_str());
            });

        interface::raii_thread thread2(
            [&]()
            {
                const uint8_t tx_data[] = "hello 2!";
                auto send_status        = client.send(tx_data);
                thread_printf("client sent: \"%s\" (size=%zu bytes, status=%s)\n",
                              tx_data, sizeof(tx_data), send_status.c_str());

                uint8_t rx_data[100] = {};
                auto rx_result_info  = client.receive(rx_data);
                thread_printf("client received: \"%s\" (size=%zu bytes, status=%s)\n",
                              rx_data, rx_result_info.size, rx_result_info.status.c_str());
            });

        interface::raii_thread thread3(
            [&]()
            {
                uint8_t rx_data[100] = {};
                auto rx_result_info  = server.receive(rx_data);
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

    // we can also .close() a socket even if multiple threads are actively trying to send or receive,
    // the active operations will be immediately interrupted and canceled
    interface::raii_thread thread5(
        [&]()
        {
            uint8_t rx_data[100] = {};
            auto rx_result_info  = server.receive(rx_data, std::chrono::seconds(300));
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