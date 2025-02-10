/// @file cpptxrx_op_types.h
/// @author Darren V Levine (DarrenVLevine@gmail.com)
/// @brief Defines the input and output parameters for each of the base operation types (send/receive/open/close)
///
/// @copyright (c) 2024 Darren V Levine. This code is licensed under MIT license (see LICENSE file for details).
///
#ifndef CPPTXRX_OP_TYPES_H_
#define CPPTXRX_OP_TYPES_H_

#include "cpptxrx_filters.h"
#include "cpptxrx_overflow.h"
#include "cpptxrx_status.h"
#include <chrono>
#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>

namespace interface
{
    /// @brief the default send and receive channel property if not used or unset
    static constexpr int default_unset_channel = -1;

    /// @brief common operation (send/receive/open/close) parameters
    struct common_op
    {
        /// @brief when the operation needs to stop/timeout by
        std::chrono::steady_clock::time_point end_time = {};

        /// @brief the status of the operation
        status_e status = status_e::START_NEW_OP;

        /// @brief ends the operation with the specified status, defaults to SUCCESS
        inline constexpr void end_op(status_e new_status = status_e::SUCCESS) noexcept
        {
            status = new_status;
        }

        /// @brief ends the operation with any user specified error code and c-string literal error message
        inline constexpr void end_op_with_error_code(unsigned int err_num, const char *&&error_num_decoder) noexcept
        {
            status.set_error_code(err_num, std::move(error_num_decoder));
        }

        inline constexpr bool is_operating() const
        {
            return status.is_operating();
        }

        /// @brief returns the duration until the end_time timeout, from the passed absolute time point
        template <typename Dur = std::chrono::nanoseconds>
        inline constexpr Dur duration_until_timeout(std::chrono::steady_clock::time_point now_time) const noexcept
        {
            if (end_time <= now_time)
                return Dur(0);
            return end_time - now_time;
        }

        /// @brief returns the duration until the end_time timeout, from the current time
        template <typename Dur = std::chrono::nanoseconds>
        inline constexpr Dur duration_until_timeout() const noexcept
        {
            return duration_until_timeout(std::chrono::steady_clock::now());
        }
    };

    /// @brief the input arguments needed to perform a send operation
    struct send_op : common_op
    {
        /// @brief the number of bytes of data to send
        size_t send_size = 0u;

        /// @brief the bytes of data to send
        const uint8_t *send_data = nullptr;

        /// @brief can be used to convey or associate what channel/port/address/etc. the send should occur on
        int send_channel = default_unset_channel;

        /// @brief resets all member variables of the struct
        inline constexpr void reset() noexcept
        {
            *this = send_op{};
        }
    };

    /// @brief the input and output arguments needed to perform a receive operation
    struct recv_op : common_op
    {
        /// @brief the maximum size of the byte array that the receive operation used
        weak_const<size_t> max_receive_size = weak_const<size_t>(0u);

        /// @brief the bytes of data received
        weak_const<uint8_t *> received_data = weak_const<uint8_t *>(nullptr);

        /// @brief the number of bytes of data received
        size_t received_size = 0u;

        /// @brief can be used to convey or associate what channel/port/address/etc. the receive occurred on
        int received_channel = default_unset_channel;

        /// @brief when reusing a receiving memory space, reset can be used to reset the class between receive
        /// operations without resetting the variables holding the pointer/size of the storage space it refers to
        inline constexpr void reset() noexcept
        {
            received_size       = 0;
            received_channel    = default_unset_channel;
            common_op::end_time = std::chrono::steady_clock::time_point::max();
            common_op::status   = status_e::standard_status_e::START_NEW_OP;
        }

        /// @brief copy the passed data into the receive buffer if they fit, and then end the receive operation
        /// with status_e::SUCCESS if all the bytes fit, or status_e::RECV_DATA_TOO_LARGE if they didn't,
        /// or status_e::RECV_DATA_NULLPTR if the data was nullptr
        ///
        /// @param    data: bytes to receive
        /// @param    size: number of bytes to receive
        /// @return   true: if the bytes fit within the max_receive_size and was not nullptr
        inline constexpr bool copy_data_and_end_op(const uint8_t *data, size_t size)
        {
            if (data == nullptr)
            {
                end_op(status_e::RECV_DATA_NULLPTR);
                return false;
            }
            if (size > max_receive_size)
            {
                end_op(status_e::RECV_DATA_TOO_LARGE);
                return false;
            }
            memcpy(received_data, data, size);
            received_size = size;
            end_op();
            return true;
        }

        /// @brief copy the passed data into the receive buffer if they fit, and then end the receive operation
        /// with status_e::SUCCESS if all the bytes fit, or status_e::RECV_DATA_TOO_LARGE if they didn't.
        ///
        /// @param    data[size]: bytes to receive as a sized array
        /// @return   true: if the bytes fit within the max_receive_size
        template <size_t size>
        constexpr bool copy_data_and_end_op(const uint8_t (&data)[size])
        {
            return copy_data_and_end_op(data, size);
        }
    };

    namespace receive_callback
    {
        /// @brief base class for the receive_callback::functional class, that defines the function
        /// and operation data but not the storage space (since that needs to be a user-specified option)
        struct abstract
        {
            recv_op op_data = {};

            constexpr abstract(recv_op &&op_data_arg) : op_data(std::move(op_data_arg)) {}
            constexpr abstract()               = default;
            virtual ~abstract()                = default;
            virtual void operator()(recv_op &) = 0;
            virtual bool is_valid()            = 0;
            virtual void reset()               = 0;
            inline void reset_all()
            {
                op_data.reset();
                reset();
            }

            /// @brief Get the name of the receive callback. Use "::name" instead for non-polymorphic constexpr name in
            /// derived classes.
            ///
            /// @return   const char*: name of callback
            virtual const char *get_name() const = 0;
        };

        /// @brief Lets you define the receive_callback operator()(...) in code by inheriting from receive_callback::base_class
        ///        instead of as data (a functional container) like in receive_callback::functional.
        ///        The callback function will run whenever a receive operation is completed as well as holds onto the storage
        ///        array of bytes to hold the received data.
        ///
        /// @tparam   derived_class: The derived class type, so that the name of the derived class can be auto-populated.
        /// @tparam   max_storage_size: maximum number of bytes to allocate storage for the receive operation.
        template <typename derived_class, size_t max_storage_size_arg = default_max_packet_size>
        struct base_class : abstract
        {
        public:
            static constexpr size_t max_storage_size = max_storage_size_arg;
            static_assert(max_storage_size > 0u, "receive callback must have non-zero storage space to perform the receive");

            /// @brief initializes the operational data in the receive_callback's base_class, such as storage pointers
            constexpr base_class()
                : abstract(
                      recv_op{
                          common_op{
                              std::chrono::steady_clock::time_point::max(),
                              status_e::standard_status_e::START_NEW_OP},
                          weak_const<size_t>{max_storage_size}, // max_receive_size
                          weak_const<uint8_t *>{storage},       // received_data
                          0u,                                   // received_size
                          default_unset_channel                 // received_channel
                      })
            {
            }

            // copy/move constructed objects are just complete resets, since memory/operations can't be shared
            constexpr base_class(base_class<derived_class, max_storage_size_arg> &&) : base_class() {}
            constexpr base_class(const base_class<derived_class, max_storage_size_arg> &) : base_class() {}

            // delete copies by assignment to reinforce that memory/operations can't be shared
            constexpr base_class &operator=(const base_class<derived_class, max_storage_size_arg> &) = delete;
            constexpr base_class &operator=(base_class<derived_class, max_storage_size_arg> &&)      = delete;

            virtual ~base_class() = default;
            virtual bool is_valid() override { return true; }
            virtual void reset() override {}
            static constexpr const char *name = introspection::type_name<derived_class>();
            const char *get_name() const final
            {
                return name;
            }

        private:
            template <typename, typename>
            friend class threadsafe_factory;

            template <typename, typename>
            friend class raw_factory;

            uint8_t storage[max_storage_size_arg] = {};
        };

        /// @brief Lets you define the callback function as data (a lambda, or std::function, etc.), instead of defining
        ///        the receive_callback::base_class operator()(...) in code by inheriting from receive_callback::base_class
        ///        The callback function will run whenever a receive operation is completed as well as holds onto the storage
        ///        array of bytes to hold the received data.
        ///
        /// @tparam   max_storage_size: maximum number of bytes to receive
        /// @tparam   F: underlying function type (a lambda, or std::function, etc.), which must have a convertible signature to "void(recv_op &)"
        template <size_t max_storage_size_arg = default_max_packet_size, typename F = int, typename std::enable_if<std::is_convertible<F, std::function<void(recv_op &)>>::value, int *>::type = nullptr>
        struct functional : public abstract
        {
        public:
            static constexpr size_t max_storage_size = max_storage_size_arg;
            static_assert(max_storage_size > 0u, "receive callback must have non-zero storage space to perform the receive");
            using func_type = std::remove_reference_t<F>;

            /// @brief Construct a operation object that stores received bytes and
            /// calls the specified callback
            ///
            /// @tparam   F: callback function type
            /// @param    callback_function: function to call when the receive is completed
            constexpr functional(func_type &&callback_function)
                : abstract(
                      recv_op{
                          common_op{
                              std::chrono::steady_clock::time_point::max(),
                              status_e::standard_status_e::START_NEW_OP},
                          weak_const<size_t>{max_storage_size}, // max_receive_size
                          weak_const<uint8_t *>{storage},       // received_data
                          0u,                                   // received_size
                          default_unset_channel                 // received_channel
                      }),
                  func{std::move(callback_function)}
            {
            }
            constexpr functional(const func_type &callback_function)
                : abstract(
                      recv_op{
                          common_op{
                              std::chrono::steady_clock::time_point::max(),
                              status_e::standard_status_e::START_NEW_OP},
                          weak_const<size_t>{max_storage_size}, // max_receive_size
                          weak_const<uint8_t *>{storage},       // received_data
                          0u,                                   // received_size
                          default_unset_channel                 // received_channel
                      }),
                  func{callback_function}
            {
            }

            // copy/move constructed objects are just complete resets, since memory/operations can't be shared
            constexpr functional(functional<max_storage_size, F> &&other) : functional(std::move(other.func)) {}
            constexpr functional(const functional<max_storage_size, F> &other) : functional(other.func) {}

            // delete copies by assignment to reinforce that memory/operations can't be shared
            constexpr functional &operator=(const functional<max_storage_size, F> &other) = delete;
            constexpr functional &operator=(functional<max_storage_size, F> &&other)      = delete;

            void operator()(recv_op &arg) final
            {
                func(arg);
            }

            bool is_valid() final { return func != nullptr; }
            static constexpr const char *name = introspection::type_name<functional<max_storage_size, F>>();
            const char *get_name() const final
            {
                return name;
            }

            void reset() final {}

        private:
            template <typename, typename>
            friend class threadsafe_factory;

            template <typename, typename>
            friend class raw_factory;

            func_type func                    = {};
            uint8_t storage[max_storage_size] = {};
        };

        /// @brief "receive_callback::create()" can turning lambdas and other functional/callable objects into a receive_callback objects.
        ///
        /// NOTE: Creating a new callback function via the long way of inheriting from receive_callback::base_class instead
        ///       of using "receive_callback::create()" can sometimes be slightly more efficient, since it can avoid
        ///       the extra functional abstraction if you use the"final" keyword to let the compiler remove virtualization".
        ///
        /// Example:
        ///     using namespace interface;
        ///     auto callback_function_that_prints = receive_callback::create<storage_size(1000)>(
        ///         [](recv_op &rx_data)
        ///         {
        ///             printf("received %zu bytes!\n", rx_data.received_size);
        ///         });
        ///
        /// @tparam   max_storage_size
        /// @tparam   F: The input function type, which will automatically resolve to the raw function type (such as the lambda type)
        ///              to avoid abstraction overhead, though the type can be a std::function if you don't care about the overhead.
        /// @param    func: the function you'd like to use to define the callback
        /// @return   functional<max_storage_size, F>: the created receive_callback object
        template <size_t max_storage_size = default_max_packet_size, typename F = int, typename std::enable_if<std::is_convertible<F, std::function<void(recv_op &)>>::value, int *>::type = nullptr>
        functional<max_storage_size, F> create(F &&func)
        {
            return {std::forward<F>(func)};
        }
    } // namespace receive_callback

    /// @brief the input arguments needed to perform a close operation
    struct close_op : common_op
    {
    };

    /// @brief type for the keep_existing_setting setting
    struct keep_existing_setting_type
    {
    };

    /// @brief specifies that the existing setting should be used
    static constexpr keep_existing_setting_type keep_existing_setting = {};

    namespace details
    {
        template <class, template <class, class...> class>
        struct is_instance_of : public std::false_type
        {
        };

        template <class... Ts, template <class, class...> class U>
        struct is_instance_of<U<Ts...>, U> : public std::true_type
        {
        };
    } // namespace details

    /// @brief specifies how an optionally applied setting should be updated
    enum class optional_setting_update_type_e : uint8_t
    {
        UPDATE,
        UPDATE_AUXILIARY_1,
        USE_EXISTING,
        USE_DEFAULT
    };

    /// @brief adds update specification information to a setting value of templated typename T
    template <typename T>
    struct optional_setting
    {
        T value                                    = {};
        optional_setting_update_type_e update_type = optional_setting_update_type_e::USE_EXISTING;

        inline constexpr bool has_update() const
        {
            return update_type == optional_setting_update_type_e::UPDATE;
        }
    };

    template <typename T>
    struct optional_setting<std::shared_ptr<T>>
    {
        std::shared_ptr<T> value                   = nullptr;
        optional_setting_update_type_e update_type = optional_setting_update_type_e::USE_EXISTING;

        inline constexpr bool has_update() const
        {
            return update_type == optional_setting_update_type_e::UPDATE;
        }
    };

    /// @brief generates a new receive callback, or send/receive filter in a shared_ptr, which is allowed as an
    /// input argument to the relevant common_opts() setter.
    /// @tparam T: the object type
    /// @param value: the input object to put in the heap
    /// @return: a shared_ptr that point to a copy (or move) of the original object now located in the heap
    template <typename T>
    std::shared_ptr<std::remove_reference_t<T>> allow_heap(T &&value)
    {
        return std::make_shared<std::remove_reference_t<T>>(std::forward<T>(value));
    }

    /// @brief type for the default_timeout option
    struct default_timeout_type
    {
    };

    /// @brief specifies that the default timeout should be used
    static constexpr default_timeout_type default_timeout = {};

    /// @brief type for the "none" option
    struct none_type
    {
    };

    /// @brief specifies that no option should be used for the setting
    static constexpr none_type none = {};

    /// @brief common open options that are not interface-specific, note: opts() are the interface-specific open options
    struct common_opts
    {
        /// @brief an open timeout to use instead fo the default value, can convey three options:
        /// A) a relative timeout
        /// B) an absolute timeout
        /// C) to use the default timeout (keeps existing default)
        /// D) interface::none -> which is just the max timeout
        optional_setting<std::chrono::steady_clock::time_point> m_timeout{};

        /// @brief a pointer to a receive_callback function container that should be called when a receive occurs
        /// WARNING: the actual lifetime of the object must be managed separately
        /// WARNING: only available in the threadsafe version, not the raw version (since there'd be no way to stop the receives
        /// from another thread in the raw version)
        optional_setting<std::shared_ptr<receive_callback::abstract>> m_callback{};

        /// @brief a pointer to a receive_filter function container that should be called when a receive occurs
        /// that sits between the actual receive and the receive shown to the user-exposed interface
        /// WARNING: the actual lifetime of the object must be managed separately
        optional_setting<std::shared_ptr<filter::abstract>> m_rx_filter{};

        /// @brief a pointer to a send_filter function container that should be called when a send occurs
        /// that sits between the actual send and the send shown to the user-exposed interface
        /// WARNING: the actual lifetime of the object must be managed separately
        optional_setting<std::shared_ptr<filter::abstract>> m_tx_filter{};

        /// @brief if the connection fails to open, or closes automatically due to an error, setting this parameter
        /// will tell the connection to just keep calling reopen automatically after the provided interval
        /// until the user manually calls .close() or the class destruction is started. Note that a negative value
        /// will disable the setting - and reopens will no longer occur automatically.
        optional_setting<std::chrono::nanoseconds> m_auto_reopen_after{};

        /// @brief disables the receive callback
        ///
        /// @param    none: no value
        /// @return   common_opts&: reference to common_opts instance
        inline common_opts &receive_callback(none_type)
        {
            m_callback.value       = nullptr;
            m_callback.update_type = optional_setting_update_type_e::UPDATE;
            return *this;
        }

        /// @brief sets the receive callback (use nullptr to disable the callback, or don't set in order to use existing setting)
        /// WARNING: only available in the threadsafe version, not the raw version (since there'd be no way to stop the receives
        /// from another thread in the raw version)
        ///
        /// @param    p_callback_arg: a pointer to a separately managed receive_callback function container
        /// @return   common_opts&: reference to common_opts instance
        inline common_opts &receive_callback(receive_callback::abstract *p_callback_arg) noexcept
        {
            if (p_callback_arg == nullptr)
                m_callback.value = nullptr;
            else
                m_callback.value = std::shared_ptr<receive_callback::abstract>(std::shared_ptr<receive_callback::abstract>{}, p_callback_arg);
            m_callback.update_type = optional_setting_update_type_e::UPDATE;
            return *this;
        }

        /// @brief sets the receive callback to use the existing callback from any previous open (which may be nullptr)
        /// WARNING: only available in the threadsafe version, not the raw version (since there'd be no way to stop the receives
        /// from another thread in the raw version)
        ///
        /// @param    keep_existing_setting: a placeholder value to indicate the keep_existing desire
        /// @return   common_opts&: reference to common_opts instance
        inline constexpr common_opts &receive_callback(keep_existing_setting_type)
        {
            m_callback.update_type = optional_setting_update_type_e::USE_EXISTING;
            return *this;
        }

        /// @brief sets the receive callback using an allow_heap(...) wrapped type
        /// WARNING: only available in the threadsafe version, not the raw version (since there'd be no way to stop the receives
        /// from another thread in the raw version)
        ///
        /// @param    callback_arg: a allow_heap() wrapped receive_callback function container
        /// @return   common_opts&: reference to common_opts instance
        template <typename T, std::enable_if_t<!std::is_pointer_v<std::remove_reference_t<T>> &&
                                                   !std::is_same_v<std::remove_reference_t<T>, std::nullptr_t> &&
                                                   !std::is_same_v<std::remove_reference_t<T>, keep_existing_setting_type>,
                                               bool> = true>
        inline common_opts &receive_callback(T &&callback_arg)
        {
            static constexpr bool was___allow_heap___specification_used = details::is_instance_of<std::remove_reference_t<T>, std::shared_ptr>{};
            static_assert(was___allow_heap___specification_used,
                          "\n"
                          "\n"
                          "You passed in an object directly, which contains arbitrary amounts of memory storage and so would require "
                          "dynamic memory allocation to copy it into the target interface class. You did:\n"
                          "\n"
                          "    .receive_callback( your_callback  )  - lvalue passing, or:\n"
                          "    .receive_callback( create(...)    )  - rvalue passing\n"
                          "\n"
                          "If you want to avoid dynamic memory allocation, pass in a raw pointer instead, and cpptxrx will just use "
                          "your object it its existing spot in memory:\n"
                          "\n"
                          "    .receive_callback( &your_callback )  - pointer passing\n"
                          "\n"
                          "If you really intended to allow the object to be put into heap memory and tracked within the interface::common_opts() "
                          "class, you can suppress this error by explicitly stating: 'allow_heap(...)', for example:\n"
                          "\n"
                          "    .receive_callback(  allow_heap(your_callback)            )  - If you want it 'copied' onto the heap\n"
                          "    .receive_callback(  allow_heap(std::move(your_callback)) )  - If you want it 'moved' onto the heap. NOTE: This "
                          "will render your original callback object unusable.\n"
                          "    .receive_callback(  allow_heap(create(...)               )  - Or just pass in an rvalue if you want it 'moved' "
                          "onto the heap without the intermediate variable.\n"
                          "\n");
            m_callback.value       = std::forward<T>(callback_arg);
            m_callback.update_type = optional_setting_update_type_e::UPDATE;
            return *this;
        }

        /// @brief disables the send filter
        ///
        /// @param    none: no value
        /// @return   common_opts&: reference to common_opts instance
        inline common_opts &send_filter(none_type)
        {
            m_tx_filter.value       = nullptr;
            m_tx_filter.update_type = optional_setting_update_type_e::UPDATE;
            return *this;
        }

        /// @brief sets the send filter (use nullptr to disable the option, or don't set in order to use existing setting)
        ///
        /// @param    p_tx_filter_arg: a pointer to a separately managed send_filter function container
        /// @return   common_opts&: reference to common_opts instance
        inline common_opts &send_filter(filter::abstract *p_tx_filter_arg) noexcept
        {
            if (p_tx_filter_arg == nullptr)
                m_tx_filter.value = nullptr;
            else
                m_tx_filter.value = std::shared_ptr<filter::abstract>(std::shared_ptr<filter::abstract>{}, p_tx_filter_arg);
            m_tx_filter.update_type = optional_setting_update_type_e::UPDATE;
            return *this;
        }

        /// @brief sets the receive callback to use the existing callback from any previous open (which may be nullptr)
        ///
        /// @param    keep_existing_setting: a placeholder value to indicate the keep_existing desire
        /// @return   common_opts&: reference to common_opts instance
        inline constexpr common_opts &send_filter(keep_existing_setting_type)
        {
            m_tx_filter.update_type = optional_setting_update_type_e::USE_EXISTING;
            return *this;
        }

        /// @brief sets the send_filter using an allow_heap(...) wrapped type
        ///
        /// @param    tx_filter_arg: a allow_heap() wrapped filter class instance
        /// @return   common_opts&: reference to common_opts instance
        template <typename T, std::enable_if_t<!std::is_pointer_v<std::remove_reference_t<T>> && !std::is_same_v<std::remove_reference_t<T>, std::nullptr_t> && !std::is_same_v<std::remove_reference_t<T>, keep_existing_setting_type>, bool> = true>
        inline common_opts &send_filter(T &&tx_filter_arg)
        {
            static constexpr bool was___allow_heap___specification_used = details::is_instance_of<std::remove_reference_t<T>, std::shared_ptr>{};
            static_assert(was___allow_heap___specification_used,
                          "\n"
                          "\n"
                          "You passed in an object directly, which contains arbitrary amounts of memory storage and so would require "
                          "dynamic memory allocation to copy it into the target interface class. You did:\n"
                          "\n"
                          "    .send_filter( your_callback  )  - lvalue passing, or:\n"
                          "    .send_filter( create(...)    )  - rvalue passing\n"
                          "\n"
                          "If you want to avoid dynamic memory allocation, pass in a raw pointer instead, and cpptxrx will just use "
                          "your object it its existing spot in memory:\n"
                          "\n"
                          "    .send_filter( &your_callback )  - pointer passing\n"
                          "\n"
                          "If you really intended to allow the object to be put into heap memory and tracked within the interface::common_opts() "
                          "class, you can suppress this error by explicitly stating: 'allow_heap(...)', for example:\n"
                          "\n"
                          "    .send_filter(  allow_heap(your_callback)            )  - If you want it 'copied' onto the heap\n"
                          "    .send_filter(  allow_heap(std::move(your_callback)) )  - If you want it 'moved' onto the heap. NOTE: This "
                          "will render your original callback object unusable.\n"
                          "    .send_filter(  allow_heap(create(...)               )  - Or just pass in an rvalue if you want it 'moved' "
                          "onto the heap without the intermediate variable.\n"
                          "\n");
            m_tx_filter.value       = std::forward<T>(tx_filter_arg);
            m_tx_filter.update_type = optional_setting_update_type_e::UPDATE;
            return *this;
        }

        /// @brief disables the receive filter
        ///
        /// @param    none: no value
        /// @return   common_opts&: reference to common_opts instance
        inline common_opts &receive_filter(none_type)
        {
            m_rx_filter.value       = nullptr;
            m_rx_filter.update_type = optional_setting_update_type_e::UPDATE;
            return *this;
        }

        /// @brief sets the receive filter (use nullptr to disable the option, or don't set in order to use existing setting)
        ///
        /// @param    p_rx_filter_arg: a pointer to a separately managed receive_filter function container
        /// @return   common_opts&: reference to common_opts instance
        inline common_opts &receive_filter(filter::abstract *p_rx_filter_arg) noexcept
        {
            if (p_rx_filter_arg == nullptr)
                m_rx_filter.value = nullptr;
            else
                m_rx_filter.value = std::shared_ptr<filter::abstract>(std::shared_ptr<filter::abstract>{}, p_rx_filter_arg);
            m_rx_filter.update_type = optional_setting_update_type_e::UPDATE;
            return *this;
        }

        /// @brief sets the receive callback to use the existing callback from any previous open (which may be nullptr)
        ///
        /// @param    keep_existing_setting: a placeholder value to indicate the keep_existing desire
        /// @return   common_opts&: reference to common_opts instance
        inline constexpr common_opts &receive_filter(keep_existing_setting_type)
        {
            m_rx_filter.update_type = optional_setting_update_type_e::USE_EXISTING;
            return *this;
        }

        /// @brief sets the receive_filter using an allow_heap(...) wrapped type
        ///
        /// @param    rx_filter_arg: a allow_heap() wrapped filter class instance
        /// @return   common_opts&: reference to common_opts instance
        template <typename T, std::enable_if_t<!std::is_pointer_v<std::remove_reference_t<T>> && !std::is_same_v<std::remove_reference_t<T>, std::nullptr_t> && !std::is_same_v<std::remove_reference_t<T>, keep_existing_setting_type>, bool> = true>
        inline common_opts &receive_filter(T &&rx_filter_arg)
        {
            static constexpr bool was___allow_heap___specification_used = details::is_instance_of<std::remove_reference_t<T>, std::shared_ptr>{};
            static_assert(was___allow_heap___specification_used,
                          "\n"
                          "\n"
                          "You passed in an object directly, which contains arbitrary amounts of memory storage and so would require "
                          "dynamic memory allocation to copy it into the target interface class. You did:\n"
                          "\n"
                          "    .receive_filter( your_callback  )  - lvalue passing, or:\n"
                          "    .receive_filter( create(...)    )  - rvalue passing\n"
                          "\n"
                          "If you want to avoid dynamic memory allocation, pass in a raw pointer instead, and cpptxrx will just use "
                          "your object it its existing spot in memory:\n"
                          "\n"
                          "    .receive_filter( &your_callback )  - pointer passing\n"
                          "\n"
                          "If you really intended to allow the object to be put into heap memory and tracked within the interface::common_opts() "
                          "class, you can suppress this error by explicitly stating: 'allow_heap(...)', for example:\n"
                          "\n"
                          "    .receive_filter(  allow_heap(your_callback)            )  - If you want it 'copied' onto the heap\n"
                          "    .receive_filter(  allow_heap(std::move(your_callback)) )  - If you want it 'moved' onto the heap. NOTE: This "
                          "will render your original callback object unusable.\n"
                          "    .receive_filter(  allow_heap(create(...)               )  - Or just pass in an rvalue if you want it 'moved' "
                          "onto the heap without the intermediate variable.\n"
                          "\n");
            m_rx_filter.value       = std::forward<T>(rx_filter_arg);
            m_rx_filter.update_type = optional_setting_update_type_e::UPDATE;
            return *this;
        }

        /// @brief sets the open operation's timeout to no timeout (wait infinitely)
        ///
        /// @param    none: no timeout
        /// @return   common_opts&: reference to common_opts instance
        inline constexpr common_opts &open_timeout(none_type)
        {
            return open_timeout(std::chrono::steady_clock::time_point::max());
        }

        /// @brief sets the open operation's timeout to a relative duration from the start of the open() call
        ///
        /// @param    val: duration to timeout
        /// @return   common_opts&: reference to common_opts instance
        template <class Rep, class Period>
        constexpr common_opts &open_timeout(std::chrono::duration<Rep, Period> val)
        {
            m_timeout.value       = std::chrono::steady_clock::time_point() + overflow_safe::duration_cast<std::chrono::nanoseconds>(val);
            m_timeout.update_type = optional_setting_update_type_e::UPDATE_AUXILIARY_1;
            return *this;
        }

        /// @brief sets the open operation's timeout to an absolute steady_clock time
        ///
        /// @param    val: steady_clock time until timeout
        /// @return   common_opts&: reference to common_opts instance
        inline constexpr common_opts &open_timeout(std::chrono::steady_clock::time_point val)
        {
            m_timeout.value       = val;
            m_timeout.update_type = optional_setting_update_type_e::UPDATE;
            return *this;
        }

        /// @brief sets the open operation's timeout to use whatever the implementations default is
        ///
        /// @param    default_timeout: a placeholder value to indicate the default time should be used
        /// @return   common_opts&: reference to common_opts instance
        inline constexpr common_opts &open_timeout(default_timeout_type)
        {
            m_timeout.update_type = optional_setting_update_type_e::USE_DEFAULT;
            return *this;
        }

        /// @brief sets the connection automatically reopen, if it closes due to an error (or fails
        /// to open), after the supplied duration.
        ///
        /// @param    val: duration to wait before each reopen retry
        /// @return   common_opts&: reference to common_opts instance
        template <class Rep, class Period>
        constexpr common_opts &auto_reopen_after(std::chrono::duration<Rep, Period> val)
        {
            m_auto_reopen_after.value       = overflow_safe::duration_cast<std::chrono::nanoseconds>(val);
            m_auto_reopen_after.update_type = optional_setting_update_type_e::UPDATE;
            return *this;
        }

        /// @brief disables automatic reopening
        ///
        /// @param    none: no automatic reopening
        /// @return   common_opts&: reference to common_opts instance
        inline constexpr common_opts &auto_reopen_after(none_type)
        {
            m_auto_reopen_after.value       = std::chrono::nanoseconds(-1);
            m_auto_reopen_after.update_type = optional_setting_update_type_e::UPDATE;
            return *this;
        }

        /// @brief sets the auto_reopen_after setting to use the existing setting from any previous open (which may be nullptr)
        ///
        /// @param    keep_existing_setting: a placeholder value to indicate the keep_existing desire
        /// @return   common_opts&: reference to common_opts instance
        inline constexpr common_opts &auto_reopen_after(keep_existing_setting_type)
        {
            m_auto_reopen_after.update_type = optional_setting_update_type_e::USE_EXISTING;
            return *this;
        }

        /// @brief gets the timeout as an absolute steady_clock::time_point time no matter what the input type was
        ///
        /// @param    default_timeout_ns: the default timeout to use if default_timeout was specified
        /// @return   std::chrono::steady_clock::time_point: when to timeout
        inline constexpr std::chrono::steady_clock::time_point resolve_open_timeout(std::chrono::nanoseconds default_timeout_ns) const
        {
            switch (m_timeout.update_type)
            {
            case optional_setting_update_type_e::UPDATE:
                return m_timeout.value;
            case optional_setting_update_type_e::UPDATE_AUXILIARY_1:
                return overflow_safe::now_plus(m_timeout.value - std::chrono::steady_clock::time_point());
            case optional_setting_update_type_e::USE_EXISTING:
                break;
            case optional_setting_update_type_e::USE_DEFAULT:
                break;
            default:
                break;
            }
            return overflow_safe::now_plus(std::chrono::nanoseconds(default_timeout_ns));
        }
    };

    /// @brief the input arguments needed to perform an open operation
    using open_op = common_op;

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
        /// @return   Dur: the duration of time until the next timeout, 0 if the timeout has already
        ///                expired, and Dur::max() if there are no operations.
        template <typename Dur = std::chrono::nanoseconds, size_t NUM_OPS>
        static constexpr Dur duration_until_timeout(const common_op *(&&ops_to_check)[NUM_OPS], std::chrono::steady_clock::time_point now_time)
        {
            auto min_time = Dur::max();
            for (size_t i = 0; i < NUM_OPS; i++)
            {
                if (ops_to_check[i] == nullptr)
                    continue;
                auto time_left = ops_to_check[i]->template duration_until_timeout<Dur>(now_time);
                if (time_left < min_time)
                    min_time = time_left;
            }
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
        static constexpr Dur duration_until_timeout(const common_op *(&&ops_to_check)[NUM_OPS])
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

        /// @brief can be used to convey or associate what channel/port/address/etc. the receive occurred on
        int channel = default_unset_channel;
    };

    /// @brief Type used to specify that no open options are needed. Allowing open to be called without
    /// options immediately.
    struct no_opts
    {
    };

    /// @brief Instance used to specify that no open options are needed. Allowing open to be called without
    /// options immediately, if in.
    inline static constexpr no_opts open_without_opts{};

    /// @brief used to convey default timeout values for each interface operation. Only used when no timeout
    /// is specfied on the operation.
    ///
    /// @tparam   arg_recv_timeout_ns (uint64_t, optional): timeout for a receive in ns. Defaults to 30 seconds.
    /// @tparam   arg_send_timeout_ns (uint64_t, optional): timeout for a send in ns. Defaults to 1 seconds.
    /// @tparam   arg_open_timeout_ns (uint64_t, optional): timeout for a open in ns. Defaults to 1 seconds.
    /// @tparam   arg_close_timeout_ns (uint64_t, optional): timeout for a close in ns. Defaults to 1 seconds.
    template <
        uint64_t arg_recv_timeout_ns  = std::chrono::nanoseconds(std::chrono::seconds(30)).count(),
        uint64_t arg_send_timeout_ns  = std::chrono::nanoseconds(std::chrono::seconds(1)).count(),
        uint64_t arg_open_timeout_ns  = std::chrono::nanoseconds(std::chrono::seconds(1)).count(),
        uint64_t arg_close_timeout_ns = std::chrono::nanoseconds(std::chrono::seconds(1)).count()>
    struct timeouts
    {
        static constexpr uint64_t recv_timeout_ns  = arg_recv_timeout_ns;
        static constexpr uint64_t send_timeout_ns  = arg_send_timeout_ns;
        static constexpr uint64_t open_timeout_ns  = arg_open_timeout_ns;
        static constexpr uint64_t close_timeout_ns = arg_close_timeout_ns;
    };
} // namespace interface

#endif // CPPTXRX_OP_TYPES_H_