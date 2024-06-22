/// @file cpptxrx_op_backend.h
/// @author Darren V Levine (DarrenVLevine@gmail.com)
/// @brief defines the backend bit-packed/masked tracking primitives that are used to coordinate operations
///
/// @copyright (c) 2024 Darren V Levine. This code is licensed under MIT license (see LICENSE file for details).
///
#ifndef CPPTXRX_OP_BACKEND_H_
#define CPPTXRX_OP_BACKEND_H_

#include <stddef.h>
#include <stdint.h>

namespace interface
{
    class backend
    {
        template <typename, uint64_t, uint64_t, uint64_t, uint64_t>
        friend class threadsafe_factory;

        template <typename, uint64_t, uint64_t, uint64_t, uint64_t>
        friend class raw_factory;

        /// @brief the supported underlying operation categories
        enum class op_category_e : int
        {
            OPEN      = 0,
            CLOSE     = 3,
            SEND      = 6,
            RECEIVE   = 9,
            DESTROY   = 12,
            CONSTRUCT = 15
        };

        /// @brief allows fast lookup and tracking of multiple simultaneous operations and their progress
        // using bit masking and shifting to quickly compare categories of bits or single operations
        struct op_bitmasks
        {
            using type = uint_fast32_t;

            // masks for each phase a operation, meant to be shifted relative to their
            // operation category
            static constexpr type REL_REQUEST  = 0b001;
            static constexpr type REL_ACCEPT   = 0b010;
            static constexpr type REL_COMPLETE = 0b100;
            static constexpr type REL_ALL      = REL_REQUEST | REL_ACCEPT | REL_COMPLETE;

            // individual bit masks for some select options
            static constexpr type NONE              = 0;
            static constexpr type OPEN_REQUEST      = REL_REQUEST << static_cast<int>(op_category_e::OPEN);
            static constexpr type ANY_OPEN          = REL_ALL << static_cast<int>(op_category_e::OPEN);
            static constexpr type CLOSE_REQUEST     = REL_REQUEST << static_cast<int>(op_category_e::CLOSE);
            static constexpr type ANY_CLOSE         = REL_ALL << static_cast<int>(op_category_e::CLOSE);
            static constexpr type SEND_REQUEST      = REL_REQUEST << static_cast<int>(op_category_e::SEND);
            static constexpr type RECEIVE_REQUEST   = REL_REQUEST << static_cast<int>(op_category_e::RECEIVE);
            static constexpr type DESTROY_REQUEST   = REL_REQUEST << static_cast<int>(op_category_e::DESTROY);
            static constexpr type ANY_DESTROY       = REL_ALL << static_cast<int>(op_category_e::DESTROY);
            static constexpr type ANY_REQUEST       = OPEN_REQUEST | CLOSE_REQUEST | SEND_REQUEST | RECEIVE_REQUEST | DESTROY_REQUEST;
            static constexpr type ANY_OPEN_OR_CLOSE = ANY_OPEN | ANY_CLOSE;

            /// @brief hold all the bit-packed information about all operations for an interface
            type value = NONE;

            /// @brief returns true if any of the ORed bit options are true
            inline constexpr bool is_any(type options) const noexcept
            {
                return (value & options) != 0u;
            }

            /// @brief returns true if the passed operation category is active in the ANY stage of progress
            inline constexpr bool is_any(op_category_e option) const noexcept
            {
                return is_any(REL_ALL << static_cast<int>(option));
            }

            /// @brief returns true if the passed operation category is active in the REQUESTED stage of progress
            inline constexpr bool is_requested(op_category_e option) const noexcept
            {
                return (value & (REL_REQUEST << static_cast<int>(option))) != 0;
            }

            /// @brief returns true if the passed operation category is active in the ACCEPTED stage of progress
            inline constexpr bool is_accepted(op_category_e option) const noexcept
            {
                return (value & (REL_ACCEPT << static_cast<int>(option))) != 0;
            }

            /// @brief returns true if the passed operation category is active in the COMPLETE stage of progress
            inline constexpr bool is_complete(op_category_e option) const noexcept
            {
                return (value & (REL_COMPLETE << static_cast<int>(option))) != 0;
            }

            // The stages/pipeline of an operation are:
            // 1.   REQUESTER:   starts the request using "start_request(op)"
            // 2.     PROCESSOR:   accepts the request using "accept_request(op)"
            // 3.       PROCESSOR:    ... processes the request ...
            // 4.     PROCESSOR:   completes the request using "complete_request(op)"
            // 5.   REQUESTER:   ends (clears) the completed request using "end_request(op)"

            /// @brief starts the passed operation category
            inline constexpr void start_request(op_category_e option) noexcept
            {
                value &= ~(REL_ALL << static_cast<int>(option));  // clear
                value |= REL_REQUEST << static_cast<int>(option); // set
            }

            /// @brief accepts the passed operation category
            inline constexpr void accept_request(op_category_e option) noexcept
            {
                value &= ~(REL_ALL << static_cast<int>(option)); // clear
                value |= REL_ACCEPT << static_cast<int>(option); // set
            }

            /// @brief completes the passed operation category
            inline constexpr void complete_request(op_category_e option) noexcept
            {
                value &= ~(REL_ALL << static_cast<int>(option));   // clear
                value |= REL_COMPLETE << static_cast<int>(option); // set
            }

            /// @brief ends the passed operation category
            inline constexpr void end_request(op_category_e option) noexcept
            {
                value &= ~(REL_ALL << static_cast<int>(option)); // clear
            }
        };
    };
} // namespace interface

#endif // CPPTXRX_OP_BACKEND_H_