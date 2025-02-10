#ifndef CPPTXRX_STATUS_H_
#define CPPTXRX_STATUS_H_

/// @brief a version number macro for CppTxRx
#define CPPTXRX_VERSION 1.1

#ifndef CPPTXRX_DISABLE_STRERRNO
#include <cstring>
#endif

namespace interface
{
    /// @brief an operation status tracking class, that behaves like an enum class, but with some extra features, like
    /// a c_str() method built-in, with has support for user-specified custom error codes
    class status_e
    {
    public:
        // in order to pretend to be an enum, the underlying value can be converted to/from the following actual enum class,
        // so that we have type safety/checking and the user can't accidentally set a value not in the enum class mapping

        /// @brief standard status values are < 0, so that the >= 0 values can be reserved for user generated custom error codes
        enum class standard_status_e : int
        {
            /// @brief the last operation was successful and is no longer being processed
            SUCCESS = -1,

            /// @brief the operation didn't succeed due to a time out
            TIMED_OUT = -2,

            /// @brief the operation didn't succeed due to being disabled
            DISABLED = -3,

            /// @brief a destroy operation interrupted and canceled the request
            CANCELED_IN_DESTROY = -4,

            /// @brief the operation failed because the connection is not open, or open_status returns this when indicating "not open"
            NOT_OPEN = -5,

            /// @brief the open op failed because the connection is already open and open_behaviour=FAIL_TO_OPEN_IF_ALREADY_OPEN
            FAILED_ALREADY_OPEN = -6,

            /// @brief an open operation failed because no open arguments were specified and no prior arguments were available
            NO_PRIOR_OPEN_ARGS = -7,

            /// @brief start a new operation to be worked on in by its corresponding "process_" call
            START_NEW_OP = -8,

            /// @brief the operation is still being worked on in subsequent "process_" calls
            OP_IN_PROGRESS = -9,

            /// @brief specifying a receive_callback in a interface::raw class isn't supported
            /// the callback needs a separate thread to continuously monitor the connection and call receive
            RECV_CALLBACK_NOT_VALID_IN_RAW = -10,

            /// @brief the open call failed due to a non-nullptr receive_callback specified with a nullptr function
            INVALID_ARG_RECV_CALLBACK_FUNC = -11,

            /// @brief the data received was too large to fit into the "receive(data, size)" call's data
            RECV_DATA_TOO_LARGE = -12,

            /// @brief a nullptr was passed into the recv data
            RECV_DATA_NULLPTR = -13,

            /// @brief an operation failed due to an ABORT return code.
            /// NOTE: see "const char *additional_error_info()" for specific the name of the filter that produced the error,
            /// in case the filter is in a chain.
            FILTER_ABORT = -14,

            /// @brief an operation failed due to the filter running out of storage space.
            /// NOTE: see "const char *additional_error_info()" for specific the name of the filter that produced the error,
            /// in case the filter is in a chain.
            FILTER_ABORT_EXCEEDED_STORAGE = -15,

            /// @brief an operation failed due to the filter getting data with an invalid format.
            /// NOTE: see "const char *additional_error_info()" for specific the name of the filter that produced the error,
            /// in case the filter is in a chain.
            FILTER_ABORT_DATA_FORMAT_ERROR = -16,

            /// @brief an operation failed due to the filter outputing a chunk of data that was too large to fit into the
            /// user's receive() call's provided target data array
            /// NOTE: see "const char *additional_error_info()" for specific the name of the filter that produced the error.
            FILTER_OUTPUT_SIZE_OVER_RX_MAX = -17,

            /// @brief an operation failed due to the filter's storage size not being set to 0 (consumed) when an input was attempted
            /// to be pushed into get_best_input_storage(). Forgetting to reset the shared storage prevents repeated filter operations.
            /// NOTE: see "const char *additional_error_info()" for specific the name of the filter that produced the error.
            FILTER_STORAGE_NOT_CONSUMED = -18,

            /// @brief call ".get_error_code()" and/or ".c_str()" to get error info for this interface specific error
            SEE_ERROR_CODE = -19
        };

        // the following static constexpr values are duplicated and exposed to appear like an enum class' value

        /// @brief the last operation was successful and is no longer being processed
        static constexpr standard_status_e SUCCESS = standard_status_e::SUCCESS;
        /// @brief the operation didn't succeed due to a time out
        static constexpr standard_status_e TIMED_OUT = standard_status_e::TIMED_OUT;
        /// @brief the operation didn't succeed due to being disabled
        static constexpr standard_status_e DISABLED = standard_status_e::DISABLED;
        /// @brief a destroy operation interrupted and canceled the request
        static constexpr standard_status_e CANCELED_IN_DESTROY = standard_status_e::CANCELED_IN_DESTROY;
        /// @brief the operation failed because the connection is not open, or open_status returns this when indicating "not open"
        static constexpr standard_status_e NOT_OPEN = standard_status_e::NOT_OPEN;
        /// @brief the open op failed because the connection is already open and open_behaviour=FAIL_TO_OPEN_IF_ALREADY_OPEN
        static constexpr standard_status_e FAILED_ALREADY_OPEN = standard_status_e::FAILED_ALREADY_OPEN;
        /// @brief an open operation failed because no open arguments were specified and no prior arguments were available
        static constexpr standard_status_e NO_PRIOR_OPEN_ARGS = standard_status_e::NO_PRIOR_OPEN_ARGS;
        /// @brief start a new operation to be worked on in by its corresponding "process_" call
        static constexpr standard_status_e START_NEW_OP = standard_status_e::START_NEW_OP;
        /// @brief the operation is still being worked on in subsequent "process_" calls
        static constexpr standard_status_e OP_IN_PROGRESS = standard_status_e::OP_IN_PROGRESS;
        /// @brief specifying a receive_callback in a interface::raw class isn't supported
        /// the callback needs a separate thread to continuously monitor the connection and call receive
        static constexpr standard_status_e RECV_CALLBACK_NOT_VALID_IN_RAW = standard_status_e::RECV_CALLBACK_NOT_VALID_IN_RAW;
        /// @brief the open call failed due to a non-nullptr receive_callback specified with a nullptr function
        static constexpr standard_status_e INVALID_ARG_RECV_CALLBACK_FUNC = standard_status_e::INVALID_ARG_RECV_CALLBACK_FUNC;

        /// @brief the data received was too large to fit into the "receive(data, size)" call's data
        static constexpr standard_status_e RECV_DATA_TOO_LARGE = standard_status_e::RECV_DATA_TOO_LARGE;

        /// @brief a nullptr was passed into the recv data
        static constexpr standard_status_e RECV_DATA_NULLPTR = standard_status_e::RECV_DATA_NULLPTR;

        /// @brief an operation failed due to an ABORT return code.
        /// NOTE: see "const char *additional_error_info()" for specific the name of the filter that produced the error,
        /// in case the filter is in a chain.
        static constexpr standard_status_e FILTER_ABORT = standard_status_e::FILTER_ABORT;

        /// @brief an operation failed due to the filter running out of storage space.
        /// NOTE: see "const char *additional_error_info()" for specific the name of the filter that produced the error,
        /// in case the filter is in a chain.
        static constexpr standard_status_e FILTER_ABORT_EXCEEDED_STORAGE = standard_status_e::FILTER_ABORT_EXCEEDED_STORAGE;

        /// @brief an operation failed due to the filter getting data with an invalid format.
        /// NOTE: see "const char *additional_error_info()" for specific the name of the filter that produced the error,
        /// in case the filter is in a chain.
        static constexpr standard_status_e FILTER_ABORT_DATA_FORMAT_ERROR = standard_status_e::FILTER_ABORT_DATA_FORMAT_ERROR;

        /// @brief an operation failed due to the filter outputing a chunk of data that was too large to fit into the
        /// user's receive() call's provided target data array
        /// NOTE: see "const char *additional_error_info()" for specific the name of the filter that produced the error.
        static constexpr standard_status_e FILTER_OUTPUT_SIZE_OVER_RX_MAX = standard_status_e::FILTER_OUTPUT_SIZE_OVER_RX_MAX;

        /// @brief an operation failed due to the filter's storage size not being set to 0 (consumed) when an input was attempted
        /// to be pushed into get_best_input_storage(). Forgetting to reset the shared storage prevents repeated filter operations.
        /// NOTE: see "const char *additional_error_info()" for specific the name of the filter that produced the error.
        static constexpr standard_status_e FILTER_STORAGE_NOT_CONSUMED = standard_status_e::FILTER_STORAGE_NOT_CONSUMED;

        /// @brief call ".get_error_code()" and/or ".c_str()" to get error info for this interface specific error
        static constexpr standard_status_e SEE_ERROR_CODE = standard_status_e::SEE_ERROR_CODE;

        /// @brief constructs a new status_e object using one of the standard status values as the default
        inline constexpr status_e(standard_status_e val) : value(static_cast<int>(val)) {}

        /// @brief implicitly converts the underlying status value into the standard_status_e type,
        /// if a custom error is used the value returned will be "standard_status_e::SEE_ERROR_CODE"
        inline constexpr operator standard_status_e() const
        {
            if (value >= 0)
                return standard_status_e::SEE_ERROR_CODE;
            return static_cast<standard_status_e>(value);
        }

        /// @brief implicitly retrieves the underlying status value as an int
        inline constexpr operator int() const
        {
            return value;
        }

        /// @brief implicitly converts the underlying status value into a bool
        /// equivalent to "status_value == status_e::SUCCESS"
        inline constexpr operator bool() const
        {
            return value == static_cast<int>(SUCCESS);
        }

        inline constexpr bool operator!=(standard_status_e other) const
        {
            return value != static_cast<int>(other);
        }
        inline constexpr bool operator==(standard_status_e other) const
        {
            return value == static_cast<int>(other);
        }

        /// @brief checks if the operation is in progress (START_NEW_OP or OP_IN_PROGRESS)
        ///
        /// @return   true if START_NEW_OP or OP_IN_PROGRESS
        inline constexpr bool is_operating() const
        {
            return value == static_cast<int>(START_NEW_OP) ||
                   value == static_cast<int>(OP_IN_PROGRESS);
        }

        /// @brief Set the underlying status to a custom error code and a c-string literal description of the error code
        ///
        /// @param    err_num: the number to associate with the error code
        /// @param    error_num_decoder: the c-string literal description of the error code
        inline constexpr void set_error_code(unsigned int err_num, const char *&&error_num_decoder) noexcept
        {
            value         = err_num;
            error_num_str = error_num_decoder;
        }

        /// @brief Set the underlying status to a pre-defined error code and add a c-string literal description of the error code
        ///
        /// @param    err_val: the enum to associate with the error
        /// @param    error_info: the c-string literal description of the additional error info
        inline constexpr void set_error_with_additional_info(standard_status_e err_val, const char *&&error_info) noexcept
        {
            value         = static_cast<int>(err_val);
            error_num_str = error_info;
        }

        /// @brief If the status is status_e::SEE_ERROR_CODE, this retrieves the error code set by set_error_code
        ///
        /// @return   constexpr unsigned int: the error code value
        inline constexpr unsigned int get_error_code() const noexcept
        {
            return value * (value >= 0);
        }

        /// @brief the status as a human readable c-string
        ///
        /// @return   const char*: status enum as a string
        inline constexpr const char *c_str() const noexcept
        {
            switch (value)
            {
            case static_cast<int>(standard_status_e::SUCCESS):
                return "SUCCESS";
            case static_cast<int>(standard_status_e::TIMED_OUT):
                return "TIMED_OUT";
            case static_cast<int>(standard_status_e::DISABLED):
                return "DISABLED";
            case static_cast<int>(standard_status_e::CANCELED_IN_DESTROY):
                return "CANCELED_IN_DESTROY";
            case static_cast<int>(standard_status_e::NOT_OPEN):
                return "NOT_OPEN";
            case static_cast<int>(standard_status_e::FAILED_ALREADY_OPEN):
                return "FAILED_ALREADY_OPEN";
            case static_cast<int>(standard_status_e::NO_PRIOR_OPEN_ARGS):
                return "NO_PRIOR_OPEN_ARGS";
            case static_cast<int>(standard_status_e::START_NEW_OP):
                return "START_NEW_OP";
            case static_cast<int>(standard_status_e::OP_IN_PROGRESS):
                return "OP_IN_PROGRESS";
            case static_cast<int>(standard_status_e::RECV_CALLBACK_NOT_VALID_IN_RAW):
                return "RECV_CALLBACK_NOT_VALID_IN_RAW";
            case static_cast<int>(standard_status_e::INVALID_ARG_RECV_CALLBACK_FUNC):
                return "INVALID_ARG_RECV_CALLBACK_FUNC";
            case static_cast<int>(standard_status_e::RECV_DATA_TOO_LARGE):
                return "RECV_DATA_TOO_LARGE";
            case static_cast<int>(standard_status_e::RECV_DATA_NULLPTR):
                return "RECV_DATA_NULLPTR";
            case static_cast<int>(standard_status_e::FILTER_ABORT):
                return "FILTER_ABORT";
            case static_cast<int>(standard_status_e::FILTER_ABORT_EXCEEDED_STORAGE):
                return "FILTER_ABORT_EXCEEDED_STORAGE";
            case static_cast<int>(standard_status_e::FILTER_ABORT_DATA_FORMAT_ERROR):
                return "FILTER_ABORT_DATA_FORMAT_ERROR";
            case static_cast<int>(standard_status_e::FILTER_OUTPUT_SIZE_OVER_RX_MAX):
                return "FILTER_OUTPUT_SIZE_OVER_RX_MAX";
            case static_cast<int>(standard_status_e::FILTER_STORAGE_NOT_CONSUMED):
                return "FILTER_STORAGE_NOT_CONSUMED";
            case static_cast<int>(standard_status_e::SEE_ERROR_CODE):
                // this is here for completeness, unless the user explicitly set SEE_ERROR_CODE,
                // which is not a good practice, they should get error_num_str instead
                return "SEE_ERROR_CODE";
            default:
                if (error_num_str != nullptr)
                    return error_num_str; // if a custom error code was used
#ifndef CPPTXRX_DISABLE_STRERRNO
                if (value > 0)
                    return std::strerror(value); // or try converting the value as an errno
#endif
                return "UNKNOWN_ERROR_NUM"; // if the user casted a nullptr as a rvalue c-string literal, or did some other brute force memory manipulation :(
            }
        }

        /// @brief any error info as a c-str pointer (error_num_str or strerror(value)), or the NO_ADDITIONAL_ERR_INFO pointer if no error exists
        ///
        /// @return   const char*: additional error info as a c-str
        inline const char *error_c_str() const noexcept
        {
            if (error_num_str != nullptr)
                return error_num_str;
#ifndef CPPTXRX_DISABLE_STRERRNO
            if (value > 0)
                return std::strerror(value);
#endif
            return NO_ADDITIONAL_ERR_INFO;
        }

        /// @brief returns any additional error info as a c-str pointer, or
        ///        NO_ADDITIONAL_ERR_INFO if no additional info exists.
        ///
        /// @return   const char*: additional error info as a c-str
        inline const char *additional_error_info() const noexcept
        {
            if (error_num_str != nullptr)
                return error_num_str;
            return NO_ADDITIONAL_ERR_INFO;
        }

        /// @brief returns true if additional error info is available in additional_error_info()
        inline constexpr bool has_additional_error_info() const noexcept
        {
            return error_num_str != nullptr && error_num_str != NO_ADDITIONAL_ERR_INFO;
        }

        /// @brief additional_error_info() will return this value if no additional
        /// info exists, so that additional_error_info() can't provide invalid memory
        static constexpr const char *NO_ADDITIONAL_ERR_INFO = "N/A";

    private:
        int value                 = static_cast<int>(standard_status_e::START_NEW_OP);
        const char *error_num_str = nullptr;
    };

    /// @brief returns the c_str version of a status enum
    ///
    /// @param    val: status value
    /// @return   const char*: status as a c_str
    inline const char *status_to_c_str(status_e val)
    {
        return val.c_str();
    }
} // namespace interface
#endif // CPPTXRX_STATUS_H_