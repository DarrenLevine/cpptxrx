/// @file cpptxrx_abstract.h
/// @author Darren V Levine (DarrenVLevine@gmail.com)
/// @brief defines a polymorphic abstract interface class that all other interfaces inherit from
///
/// @copyright (c) 2024 Darren V Levine. This code is licensed under MIT license (see LICENSE file for details).
///
#ifndef CPPTXRX_ABSTRACT_H_
#define CPPTXRX_ABSTRACT_H_

#include "cpptxrx_op_types.h"

namespace interface
{
    /// @brief an abstract interface that any cpptxrx interface can be interpreted as, for polymorphism
    struct abstract
    {
    public:
        /// @brief destroys the interface when you're done with it, canceling all active operations
        virtual void destroy() = 0;

        /// @brief allow interfaces to be named by overriding this method
        /// @return an optional c-str name
        virtual const char *name() const = 0;

        /// @brief allow interfaces to be numbered with an id by overriding this method
        /// @return an optional id number
        virtual int id() const = 0;

        /// @brief returns true if the interface is threadsafe
        virtual bool is_threadsafe() const noexcept = 0;

        /// @brief returns true if the connection is currently open
        virtual bool is_open() const = 0;

        /// @brief implicit conversion to a bool which is true if is_open
        inline operator bool() const { return is_open(); }

        /// @brief returns the status of the most recent open operation, or in the case of a spontaneous closure, the error
        virtual status_e open_status() const = 0;

        /// @brief reopen using old settings (reopen will close first if already open - use open if not desired), with open common_opts options
        virtual status_e reopen(common_opts) = 0;

        /// @brief reopen using old settings (reopen will close first if already open - use open if not desired)
        virtual status_e reopen() = 0;

        /// @brief open using old settings (open will fail if already open - use reopen if not desired), with open common_opts options
        virtual status_e open(common_opts) = 0;

        /// @brief open using old settings (open will fail if already open - use reopen if not desired)
        virtual status_e open() = 0;

        /// @brief closes the connection, with an absolute timeout
        virtual status_e close(std::chrono::steady_clock::time_point) = 0;

        /// @brief closes the connection, with a default timeout
        virtual status_e close() = 0;

        /// @brief closes the connection, with a relative timeout
        template <class Rep, class Period>
        status_e close(std::chrono::duration<Rep, Period> rel_timeout)
        {
            return close(overflow_safe::now_plus(rel_timeout));
        }

        /// @brief receives bytes on the connection, with an absolute timeout
        ///
        /// @param    data: where the received data will be output
        /// @param    size: the max number of bytes that can be received
        /// @param    abs_timeout: how long to wait for a receive
        /// @return   recv_ret: the final status, and number of bytes received
        virtual recv_ret receive(uint8_t *const data, size_t size, std::chrono::steady_clock::time_point abs_timeout) = 0;

        /// @brief receives bytes on the connection, with a default timeout
        ///
        /// @param    data: where the received data will be output
        /// @param    size: the max number of bytes that can be received
        /// @return   recv_ret: the final status, and number of bytes received
        virtual recv_ret receive(uint8_t *const data, size_t size) = 0;

        /// @brief receives bytes on the connection, with a relative timeout
        ///
        /// @param    data: where the received data will be output
        /// @param    size: the max number of bytes that can be received
        /// @param    rel_timeout: how long to wait for a receive
        /// @return   recv_ret: the final status, and number of bytes received
        template <class Rep, class Period>
        recv_ret receive(uint8_t *const data, size_t size, std::chrono::duration<Rep, Period> rel_timeout)
        {
            return receive(data, size, overflow_safe::now_plus(rel_timeout));
        }

        /// @brief receives bytes on the connection, with a default timeout
        ///
        /// @param    data: where the received data will be output
        /// @tparam   size: the max number of bytes that can be received
        /// @return   recv_ret: the final status, and number of bytes received
        template <size_t size>
        recv_ret receive(uint8_t (&data)[size])
        {
            return receive(data, size);
        }

        /// @brief receives bytes on the connection, with an absolute timeout
        ///
        /// @param    data: where the received data will be output
        /// @tparam   size: the max number of bytes that can be received
        /// @param    timeout: how long to wait for a receive
        /// @return   recv_ret: the final status, and number of bytes received
        template <size_t size>
        recv_ret receive(uint8_t (&data)[size], std::chrono::steady_clock::time_point timeout)
        {
            return receive(data, size, timeout);
        }

        /// @brief receives bytes on the connection, with a relative timeout
        ///
        /// @param    data: where the received data will be output
        /// @tparam   size: the max number of bytes that can be received
        /// @param    rel_timeout: how long to wait for a receive
        /// @return   recv_ret: the final status, and number of bytes received
        template <size_t size, class Rep, class Period>
        recv_ret receive(uint8_t (&data)[size], std::chrono::duration<Rep, Period> rel_timeout)
        {
            return receive(data, size, overflow_safe::now_plus(rel_timeout));
        }

        /// @brief sends bytes over the connection, with an absolute timeout and over a specific send channel
        ///
        /// @param    channel: an application/connection-specific channel id to send over
        /// @param    data: the bytes to send
        /// @param    size: the number of bytes to send
        /// @param    abs_timeout: how long to wait for the send to occur
        /// @return   status_e: the resulting status of the send
        virtual status_e send(int channel, const uint8_t *const data, size_t size, std::chrono::steady_clock::time_point abs_timeout) = 0;

        /// @brief sends bytes over the connection, with a default timeout and over a specific send channel
        ///
        /// @param    channel: an application/connection-specific channel id to send over
        /// @param    data: the bytes to send
        /// @param    size: the number of bytes to send
        /// @return   status_e: the resulting status of the send
        virtual status_e send(int channel, const uint8_t *const data, size_t size) = 0;

        /// @brief sends char-bytes over the connection, with a default timeout and over a specific send channel
        ///
        /// @param    channel: an application/connection-specific channel id to send over
        /// @param    data: the bytes to send
        /// @param    size: the number of bytes to send
        /// @return   status_e: the resulting status of the send
        template <typename... Targs>
        inline status_e send(int channel, const char *const data, Targs... args)
        {
            return send(channel, reinterpret_cast<const uint8_t *const>(data), std::forward<Targs>(args)...);
        }

        /// @brief sends bytes over the connection, with an absolute timeout
        ///
        /// @param    data: the bytes to send
        /// @param    size: the number of bytes to send
        /// @param    abs_timeout: how long to wait for the send to occur
        /// @return   status_e: the resulting status of the send
        template <typename T>
        status_e send(const T *const data, size_t size, std::chrono::steady_clock::time_point abs_timeout)
        {
            return send(default_unset_channel, data, size, abs_timeout);
        }

        /// @brief sends bytes over the connection, with a relative timeout
        ///
        /// @param    data: the bytes to send
        /// @param    size: the number of bytes to send
        /// @param    rel_timeout: how long to wait for the send to occur
        /// @return   status_e: the resulting status of the send
        template <class Rep, class Period, typename T>
        status_e send(const T *const data, size_t size, std::chrono::duration<Rep, Period> rel_timeout)
        {
            return send(default_unset_channel, data, size, overflow_safe::now_plus(rel_timeout));
        }

        /// @brief sends bytes over the connection, with a default timeout
        ///
        /// @param    data: the bytes to send
        /// @param    size: the number of bytes to send
        /// @return   status_e: the resulting status of the send
        template <typename T>
        inline status_e send(const T *const data, size_t size)
        {
            return send(default_unset_channel, data, size);
        }

        /// @brief sends bytes over the connection, with a relative timeout and over a specific send channel
        ///
        /// @param    channel: an application/connection-specific channel id to send over
        /// @param    data: the bytes to send
        /// @param    size: the number of bytes to send
        /// @param    rel_timeout: how long to wait for the send to occur
        /// @return   status_e: the resulting status of the send
        template <class Rep, class Period, typename T>
        status_e send(int channel, const T *const data, size_t size, std::chrono::duration<Rep, Period> rel_timeout)
        {
            return send(channel, data, size, overflow_safe::now_plus(rel_timeout));
        }

        /// @brief sends bytes over the connection, with a default timeout
        ///
        /// @param    data: the bytes to send
        /// @tparam   size: the number of bytes to send
        /// @return   status_e: the resulting status of the send
        template <size_t size, typename T>
        status_e send(const T (&data)[size])
        {
            return send(default_unset_channel, data, size);
        }

        /// @brief sends bytes over the connection, with an absolute timeout
        ///
        /// @param    data: the bytes to send
        /// @tparam   size: the number of bytes to send
        /// @param    timeout: how long to wait for a receive
        /// @return   status_e: the resulting status of the send
        template <size_t size, typename T>
        status_e send(const T (&data)[size], std::chrono::steady_clock::time_point timeout)
        {
            return send(default_unset_channel, data, size, timeout);
        }

        /// @brief sends bytes over the connection, with a relative timeout
        ///
        /// @param    data: the bytes to send
        /// @tparam   size: the number of bytes to send
        /// @param    rel_timeout: how long to wait for a receive
        /// @return   status_e: the resulting status of the send
        template <size_t size, class Rep, class Period, typename T>
        status_e send(const T (&data)[size], std::chrono::duration<Rep, Period> rel_timeout)
        {
            return send(default_unset_channel, data, size, overflow_safe::now_plus(rel_timeout));
        }

        /// @brief sends bytes over the connection, with a default timeout
        ///
        /// @param    channel: an application/connection-specific channel id to send over
        /// @param    data: the bytes to send
        /// @tparam   size: the number of bytes to send
        /// @return   status_e: the resulting status of the send
        template <size_t size, typename T>
        status_e send(int channel, const T (&data)[size])
        {
            return send(channel, data, size);
        }

        /// @brief sends bytes over the connection, with an absolute timeout
        ///
        /// @param    channel: an application/connection-specific channel id to send over
        /// @param    data: the bytes to send
        /// @tparam   size: the number of bytes to send
        /// @param    timeout: how long to wait for a receive
        /// @return   status_e: the resulting status of the send
        template <size_t size, typename T>
        status_e send(int channel, const T (&data)[size], std::chrono::steady_clock::time_point timeout)
        {
            return send(channel, data, size, timeout);
        }

        /// @brief sends bytes over the connection, with a relative timeout
        ///
        /// @param    channel: an application/connection-specific channel id to send over
        /// @param    data: the bytes to send
        /// @tparam   size: the number of bytes to send
        /// @param    rel_timeout: how long to wait for a receive
        /// @return   status_e: the resulting status of the send
        template <size_t size, class Rep, class Period, typename T>
        status_e send(int channel, const T (&data)[size], std::chrono::duration<Rep, Period> rel_timeout)
        {
            return send(channel, data, size, overflow_safe::now_plus(rel_timeout));
        }

        // boilerplate:
        virtual ~abstract()                  = default;
        abstract()                           = default;
        abstract operator=(const abstract &) = delete;
        abstract(const abstract &)           = delete;
    };

    /// @brief the subset of member variables modifiable in a transaction (during the process_<open/close/send/receive> methods)
    ///
    /// @tparam   open_opts_type: your interface's open opts type
    template <typename open_opts_type>
    struct transactions_args
    {
        /// @brief this contains pointers to any active transactions (send, receive, open, close), which are nullptr when not active
        op_instructions transactions{};

        /// @brief if the open status needs to be changed inside of the process_<open/close/send/receive> methods
        /// such as marked closed due to an error or otherwise, you can set this variable directly
        status_e m_open_status{status_e::NOT_OPEN};

        /// @brief last connection-specific options passed into an open() call. Note that they are freely modifiable in the process_<open/close/send/receive>
        /// methods, and can be queried and set asynchronously from other threads using the "get_open_args" and "set_open_args" methods if the derived interface needs to.
        open_opts_type m_open_opts{};

        // boilerplate:
        virtual ~transactions_args() = default;
    };

} // namespace interface

#endif // CPPTXRX_ABSTRACT_H_