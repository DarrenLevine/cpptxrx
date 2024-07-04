/// @file cpptxrx_op_types.h
/// @author Darren V Levine (DarrenVLevine@gmail.com)
/// @brief Defines the input and output parameters for each of the base operation types (send/receive/open/close)
///
/// @copyright (c) 2024 Darren V Levine. This code is licensed under MIT license (see LICENSE file for details).
///
#ifndef CPPTXRX_OP_TYPES_H_
#define CPPTXRX_OP_TYPES_H_

#include "cpptxrx_status.h"
#include <chrono>
#include <utility>

namespace interface
{
    /// @brief common operation (send/receive/open/close) parameters
    struct common_op
    {
        /// @brief when the operation needs to stop/timeout by
        const std::chrono::steady_clock::time_point end_time = {};

        /// @brief the status of the operation
        status_e status = status_e::IN_PROGRESS;

        /// @brief ends the operation with the specified status, defaults to SUCCESS
        inline void end_op(status_e new_status = status_e::SUCCESS)
        {
            status = new_status;
        }

        /// @brief ends the operation with any user specified error code and c-string literal error message
        inline void end_op_with_error_code(unsigned int err_num, const char *&&error_num_decoder)
        {
            status.set_error_code(err_num, std::forward<const char *&&>(error_num_decoder));
        }

        /// @brief returns the duration until the end_time timeout, from the passed absolute time point
        template <typename Dur = std::chrono::nanoseconds>
        Dur duration_until_timeout(std::chrono::steady_clock::time_point now_time) const
        {
            if (end_time <= now_time)
                return Dur(0);
            return end_time - now_time;
        }

        /// @brief returns the duration until the end_time timeout, from the current time
        template <typename Dur = std::chrono::nanoseconds>
        Dur duration_until_timeout() const
        {
            return duration_until_timeout(std::chrono::steady_clock::now());
        }
    };

    /// @brief the input arguments needed to perform a send operation
    struct send_op : common_op
    {
        const uint8_t *const send_data = nullptr;
        const size_t send_size         = 0u;
    };

    /// @brief the input and output arguments needed to perform a receive operation
    struct recv_op : common_op
    {
        uint8_t *const received_data  = nullptr;
        const size_t max_receive_size = 0u;
        size_t returned_recv_size     = 0u;
    };

    /// @brief the input arguments needed to perform a close operation
    struct close_op : common_op
    {
    };

    /// @brief the input arguments needed to perform an open operation
    struct open_op : common_op
    {
    };

    /// @brief all operation instructions, held as a set of pointers, so that the passing of the options
    // can be efficient, and so that operations can be invalidated/inactive by making them nullptr
    struct op_instructions
    {
        send_op *p_send_op     = nullptr; // a pointer to an active send operation's arguments, or nullptr for no send operation
        recv_op *p_recv_op     = nullptr; // a pointer to an active receive operation's arguments, or nullptr for no receive operation
        open_op *p_open_op     = nullptr; // a pointer to an active open operation's arguments, or nullptr for no open operation
        close_op *p_close_op   = nullptr; // a pointer to an active close operation's arguments, or nullptr for no close operation
        bool idle_in_send_recv = false;   // set to true to wait (idle) in "process_send_receive", even if no operation is requested

        /// @brief calculate the smallest duration until the timeout of the passed operation pointers (which are allowed to be nullptr)
        /// relative to the passed absolute time
        ///
        /// @tparam   Dur: duration type to return
        /// @tparam   NUM_OPS: number of operations to check
        /// @param    ops_to_check: the rvalue list of operation pointers to check
        /// @param    now_time: the absolute time to calculate the duration relative to
        /// @return   Dur: the duration of time until the next timeout, 0 if the timeout has already expired
        template <typename Dur = std::chrono::nanoseconds, size_t NUM_OPS>
        static Dur duration_until_timeout(const common_op *(&&ops_to_check)[NUM_OPS], std::chrono::steady_clock::time_point now_time)
        {
            auto min_time     = Dur::max();
            bool no_end_times = true;
            for (size_t i = 0; i < NUM_OPS; i++)
            {
                if (ops_to_check[i] == nullptr)
                    continue;
                no_end_times   = false;
                auto time_left = ops_to_check[i]->template duration_until_timeout<Dur>(now_time);
                if (time_left < min_time)
                    min_time = time_left;
            }
            if (no_end_times)
                return Dur(0);
            return min_time;
        }

        /// @brief calculate the smallest duration until the timeout of the passed operation pointers (which are allowed to be nullptr)
        /// relative to the current time
        ///
        /// @tparam   Dur: duration type to return
        /// @tparam   NUM_OPS: number of operations to check
        /// @param    ops_to_check: the rvalue list of operation pointers to check
        /// @return   Dur: the duration of time until the next timeout, 0 if the timeout has already expired
        template <typename Dur = std::chrono::nanoseconds, size_t NUM_OPS>
        static Dur duration_until_timeout(const common_op *(&&ops_to_check)[NUM_OPS])
        {
            return duration_until_timeout(std::forward<const common_op *(&&)[NUM_OPS]>(ops_to_check), std::chrono::steady_clock::now());
        }
    };

    //// @brief the return type for a receive operation, containing the final receive status, and the number of byte received
    struct recv_ret
    {
        /// @brief the final receive status
        status_e status;

        /// @brief the number of bytes received
        size_t size;
    };

    //// @brief used to specify that no open options are needed (allows open to be called without options immediately)
    struct no_opts
    {
    };
} // namespace interface

#endif // CPPTXRX_OP_TYPES_H_