#ifndef CPPTXRX_STATUS_H_
#define CPPTXRX_STATUS_H_

/// @brief a version number macro for CppTxRx
#define CPPTXRX_VERSION 1.0

namespace interface
{
    /// @brief a operation status tracking class, that behaves like an enum class, but with some extra features, like
    /// a c_str() method built-in, with has support for user-specified custom error codes
    class status_e
    {
    public:
        // in order to pretend to be an enum, the underlying value can be converted to/from the following actual enum class,
        // so that we have type safety/checking and the user can't accidentally set a value not in the enum class mapping

        /// @brief standard status values are < 0, so that the >= 0 values can be reserved for user generated custom error codes
        enum class standard_status_e : int
        {
            /// @brief the operation was successful
            SUCCESS = -1,

            /// @brief the operation didn't succeed due to a time out
            TIMED_OUT = -2,

            /// @brief a destroy operation interrupted and canceled the request
            CANCELED_IN_DESTROY = -3,

            /// @brief the operation failed because the connection is not open, or open_status returns this when indicating "not open"
            NOT_OPEN = -4,

            /// @brief the open op failed because the connection is already open and open_behaviour=FAIL_TO_OPEN_IF_ALREADY_OPEN
            FAILED_ALREADY_OPEN = -5,

            /// @brief an open operation failed because no open arguments were specified and no prior arguments were available
            NO_PRIOR_OPEN_ARGS = -6,

            /// @brief the operation is still being worked on
            IN_PROGRESS = -7,

            /// @brief call ".get_error_code()" and/or ".c_str()" to get error info for this interface specific error
            SEE_ERROR_CODE = -8
        };

        // the following static constexpr values are duplicated and exposed to appear like an enum class' value

        /// @brief the operation was successful
        static constexpr standard_status_e SUCCESS = standard_status_e::SUCCESS;
        /// @brief the operation didn't succeed due to a time out
        static constexpr standard_status_e TIMED_OUT = standard_status_e::TIMED_OUT;
        /// @brief a destroy operation interrupted and canceled the request
        static constexpr standard_status_e CANCELED_IN_DESTROY = standard_status_e::CANCELED_IN_DESTROY;
        /// @brief the operation failed because the connection is not open, or open_status returns this when indicating "not open"
        static constexpr standard_status_e NOT_OPEN = standard_status_e::NOT_OPEN;
        /// @brief the open op failed because the connection is already open and open_behaviour=FAIL_TO_OPEN_IF_ALREADY_OPEN
        static constexpr standard_status_e FAILED_ALREADY_OPEN = standard_status_e::FAILED_ALREADY_OPEN;
        /// @brief an open operation failed because no open arguments were specified and no prior arguments were available
        static constexpr standard_status_e NO_PRIOR_OPEN_ARGS = standard_status_e::NO_PRIOR_OPEN_ARGS;
        /// @brief the operation is still being worked on
        static constexpr standard_status_e IN_PROGRESS = standard_status_e::IN_PROGRESS;
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

        /// @brief Set the underlying status to a custom error code and a c-string literal description of the error code
        ///
        /// @param    err_num: the number to associate with the error code
        /// @param    error_num_decoder: the c-string literal description of the error code
        inline constexpr void set_error_code(unsigned int err_num, const char *&&error_num_decoder) noexcept
        {
            value         = err_num;
            error_num_str = error_num_decoder;
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
        inline const char *c_str() const noexcept
        {
            switch (value)
            {
            case static_cast<int>(standard_status_e::SUCCESS):
                return "SUCCESS";
            case static_cast<int>(standard_status_e::TIMED_OUT):
                return "TIMED_OUT";
            case static_cast<int>(standard_status_e::CANCELED_IN_DESTROY):
                return "CANCELED_IN_DESTROY";
            case static_cast<int>(standard_status_e::NOT_OPEN):
                return "NOT_OPEN";
            case static_cast<int>(standard_status_e::FAILED_ALREADY_OPEN):
                return "FAILED_ALREADY_OPEN";
            case static_cast<int>(standard_status_e::NO_PRIOR_OPEN_ARGS):
                return "NO_PRIOR_OPEN_ARGS";
            case static_cast<int>(standard_status_e::IN_PROGRESS):
                return "IN_PROGRESS";
            case static_cast<int>(standard_status_e::SEE_ERROR_CODE):
                // this is here for completeness, unless the user explicitly set SEE_ERROR_CODE,
                // which is not a good practice, they should get error_num_str instead
                return "SEE_ERROR_CODE";
            default:
                if (error_num_str != nullptr)
                    return error_num_str;   // if a custom error code was used
                return "UNKNOWN_ERROR_NUM"; // if the user casted a nullptr as a rvalue c-string literal :(
            }
        }

    private:
        int value                 = static_cast<int>(standard_status_e::IN_PROGRESS);
        const char *error_num_str = nullptr;
    };
} // namespace interface
#endif // CPPTXRX_STATUS_H_