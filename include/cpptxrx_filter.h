/// @file cpptxrx_filter.h
/// @author Darren V Levine (DarrenVLevine@gmail.com)
/// @brief Defines data filters, which are encapsulated/modular data processing blocks that can chain together
/// and be applied to arbitrary cpptxrx interfaces.
///
/// @copyright (c) 2024 Darren V Levine. This code is licensed under MIT license (see LICENSE file for details).
///
#ifndef CPPTXRX_FILTER_H_
#define CPPTXRX_FILTER_H_
#include <stddef.h>
#include <stdint.h>
#include <utility>

namespace introspection
{
    namespace details
    {
        template <typename T>
        struct type_wrapper
        {
            using type = T;
        };
        template <typename T>
        constexpr const char *function_name()
        {
#if defined(__clang__) || defined(__GNUC__)
            return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
            return __FUNCSIG__;
#else
            return "type_wrapper<unknown - type_name is unsupported by your compiler>";
#endif
        };
        constexpr size_t constexpr_strlen(const char *str)
        {
            const char *s = str;
            while (*s)
                ++s;
            return (s - str);
        }
        template <typename T>
        struct typename_array
        {
            char values[constexpr_strlen(function_name<T>())] = {};
        };
        template <typename T>
        constexpr typename_array<T> type_name_impl()
        {
            constexpr const char prefix[] = "type_wrapper<";
            const char *s                 = function_name<T>();
            const char *p                 = prefix;
            for (; *s && *p; ++s)
            {
                if (*s == *p)
                    ++p;
                else
                    p = prefix;
            }
            typename_array<T> value;

            // if prefix couldn't be found, then return the whole function name as is
            if (*p)
            {
                s        = function_name<T>();
                size_t i = 0;
                while (*s)
                {
                    value.values[i] = *s;
                    ++s;
                    ++i;
                }
                return value;
            }

            // otherwise, isolate to just the template name inside the wrapper
            size_t i = 0;
            while (*s)
            {
                value.values[i] = *s;
                ++s;
                ++i;
            }
            // remove up to the last >
            while (value.values[--i] != '>')
                value.values[i] = 0;

            // remove the last >
            value.values[--i] = 0;

            // remove any spaces at the end of the str
            while (value.values[i] == ' ')
                value.values[i--] = 0;
            return value;
        }
        template <typename T, typename wrapped_type = type_wrapper<T>>
        struct type_name_container
        {
            static constexpr typename_array<wrapped_type> value = type_name_impl<wrapped_type>();
        };
    } // namespace details

    // A constexpr type name getter function that only requires  >= C++11.
    // Other common implementations require string_view which is C++17.
    template <typename T>
    constexpr const char *type_name()
    {
        return details::type_name_container<T>::value.values;
    }
} // namespace introspection

namespace interface
{
#ifndef CPPTXRX_DEFAULT_MAX_PACKET_SIZE
#define CPPTXRX_DEFAULT_MAX_PACKET_SIZE 1500
#endif

    static constexpr size_t default_max_packet_size = CPPTXRX_DEFAULT_MAX_PACKET_SIZE;

    // makes a variable harder to accidentally modify, for items that should be read-only in all but a select few cases
    template <typename T>
    struct weak_const
    {
        inline constexpr const T &get() const
        {
            return value;
        }
        inline constexpr operator T() const
        {
            return value;
        }

        // ensure the only way to make a copy is by first explicitly constructing a new weak_const object
        constexpr explicit weak_const(T &&arg) : value{std::move(arg)} {}
        constexpr explicit weak_const(const T &arg) : value{arg} {}
        constexpr weak_const() = default;

    private:
        T value;
    };

    /// @brief generic for-range loop iterator class for the data_t and storage_abstract_t classes
    ///
    /// @tparam   T: type to iterator over
    template <typename T>
    class iterator
    {
    public:
        constexpr iterator(T *start_ptr, size_t offset) : ptr(start_ptr == nullptr ? start_ptr : start_ptr + offset) {}
        constexpr iterator<T> &operator++()
        {
            ++ptr;
            return *this;
        }
        constexpr bool operator!=(const iterator<T> &other) const { return ptr != other.ptr; }
        constexpr const T &operator*() const { return *ptr; }

    private:
        T *ptr;
    };

    /// @brief for specifying the number of bytes in the storage array in an easier to read way
    ///
    /// @param    value: storage value
    /// @return   constexpr size_t: the value inputted
    inline constexpr size_t storage_size(size_t value)
    {
        return value;
    }

    namespace filter
    {
        struct data_t;

        /// @brief an abstract class for storage polymorphism, containing a size, max size, and
        /// data pointer that points to actual storage in the derived class
        struct storage_abstract_t
        {
            /// @brief max number of bytes in the data field
            interface::weak_const<size_t> max_size = interface::weak_const<size_t>(0u);

            /// @brief pointer to the stored data array
            interface::weak_const<uint8_t *> data = interface::weak_const<uint8_t *>(nullptr);

            /// @brief current number of bytes in the data field
            size_t size = 0;

            inline constexpr iterator<uint8_t> begin() const { return iterator<uint8_t>(data.get(), 0u); }
            inline constexpr iterator<uint8_t> end() const { return iterator<uint8_t>(data.get(), size); }

            /// @brief appends a single value to the end of the data array
            /// and increments the size.
            ///
            /// @param    appended_value: value to append
            /// @return   false if it wouldn't fit, true if it was appended
            inline constexpr bool append(uint8_t appended_value)
            {
                if (size >= max_size)
                    return false;
                data[size++] = appended_value;
                return true;
            }

            /// @brief appends an array of values to the end of the data array
            /// and increments the size by the appending size.
            ///
            /// @param    appended_data: data to append
            /// @param    appended_data_size: size of data to append
            /// @return   false if it wouldn't fit, true if it was appended
            inline constexpr bool append(const uint8_t *appended_data, const size_t appended_data_size)
            {
                const size_t final_size = size + appended_data_size;
                if (final_size > max_size || data == nullptr || appended_data == nullptr)
                    return false;
                for (size_t i = size; i < final_size; i++)
                    data[i] = *(appended_data++);
                size = final_size;
                return true;
            }

            template <size_t appended_data_size>
            inline constexpr bool append(const uint8_t (&appended_data)[appended_data_size])
            {
                return append(appended_data, appended_data_size);
            }
            template <size_t appended_data_size>
            inline constexpr bool append(const char (&appended_data)[appended_data_size])
            {
                return append(reinterpret_cast<const uint8_t *>(appended_data), appended_data_size - 1u * (appended_data_size > 0u));
            }

            template <typename T>
            storage_abstract_t &operator<<(T &&arg)
            {
                append(arg);
                return *this;
            }

            /// @brief appends a data_t container of values to the end of the data array
            /// and increments the size by the appending size.
            ///
            /// @param    appended_data: data_t data to append
            /// @return   false if it wouldn't fit, true if it was appended
            inline constexpr bool append(const data_t &appended_data);

            /// @brief sets the size to zero, but does not clear the actual memory
            inline constexpr void reset()
            {
                size = 0;
            }

            /// @brief returns true if any data exists in the array of bytes
            inline constexpr operator bool() const
            {
                return size > 0u && data.get() != nullptr;
            }
        };

        /// @brief stores an array of bytes of a max templated size
        /// @tparam max_size_arg: the max number of stored bytes
        template <size_t max_size_arg>
        struct storage_t : storage_abstract_t
        {
            static_assert(max_size_arg > 0u, "storage size must be larger than 0");
            static constexpr size_t max_size = max_size_arg;
            storage_t() : storage_abstract_t{weak_const<size_t>(max_size_arg), weak_const<uint8_t *>(underlying_storage)} {}

            constexpr storage_t(const storage_t<max_size_arg> &other)
                : storage_abstract_t{weak_const<size_t>(max_size_arg), weak_const<uint8_t *>(underlying_storage), other.size},
                  underlying_storage{}
            {
                for (size_t i = 0; i < other.size; i++)
                    underlying_storage[i] = other.underlying_storage[i];
            }
            constexpr storage_t(storage_t<max_size_arg> &&other)
                : storage_abstract_t{weak_const<size_t>(max_size_arg), weak_const<uint8_t *>(underlying_storage), other.size},
                  underlying_storage{} {}
            constexpr storage_t &operator=(const storage_t<max_size_arg> &other)
            {
                size = other.size;
                for (size_t i = 0; i < other.size; i++)
                    underlying_storage[i] = other.underlying_storage[i];
                return *this;
            }
            constexpr storage_t &operator=(storage_t<max_size_arg> &&other)
            {
                size = other.size;
                for (size_t i = 0; i < other.size; i++)
                    underlying_storage[i] = other.underlying_storage[i];
                return *this;
            }

        private:
            uint8_t underlying_storage[max_size_arg] = {};
        };

        /// @brief a pointer to a data array of bytes, and its size, with no information
        /// about the underlying storage size or storage location.
        ///
        /// When passed through a data "filter" processing block, the lifetime of the data object can
        /// be thought of in terms of starting and ending as needed:
        ///    Output data can only start being output and exist/be-assigned by using start(...)
        /// And:
        ///    Input data can be consumed and marked as not needed anymore (it's lifetime ended) by using stop().
        struct data_t
        {
            const uint8_t *data = nullptr;
            size_t size         = 0u;

            inline constexpr iterator<const uint8_t> begin() const { return iterator<const uint8_t>(data, 0u); }
            inline constexpr iterator<const uint8_t> end() const { return iterator<const uint8_t>(data, size); }

            /// @brief returns true if any data exists in the array of bytes
            inline constexpr operator bool() const
            {
                return size > 0u && data != nullptr;
            }

            /// @brief Stops/discards the lifetime of the data. By setting data=nullptr and size=0.
            inline constexpr void stop()
            {
                data = nullptr;
                size = 0u;
            }

            /// @brief Starts the lifetime of the data by pointing it at the passed storage,
            /// and then empties the passed storage in a way that doesn't effect the started data.
            ///
            /// @param    storage: the stored data to take on, and then delete
            inline constexpr void start_and_consume(storage_abstract_t &storage)
            {
                data = storage.data;
                size = storage.size;
                storage.reset();
            }

            /// @brief Starts the lifetime of the data by setting it equal to the input arguments.
            ///
            /// @param    storage: the stored data to point the current data to
            /// @param    storage_size: the stored data size to set the current size to
            inline constexpr void start(const uint8_t *storage, size_t storage_size)
            {
                data = storage;
                size = storage_size;
            }

            /// @brief Starts the lifetime of the data by setting it equal to the input storage.
            ///
            /// @param    storage: the stored data and size to point the current data and size to
            inline constexpr void start(const storage_abstract_t &storage)
            {
                data = storage.data;
                size = storage.size;
            }

            /// @brief Starts the lifetime of the passed data, by setting it equal to the current data,
            /// and then ends the current data so that only the passed data object has the final data.
            /// @param    other
            inline constexpr void pass_to(data_t &other)
            {
                other.data = data;
                other.size = size;
                stop();
            }

            /// @brief copy data into storage, but if it's already in storage, skip the copy
            /// and just make sure the size matches what the storage has
            ///
            /// @param    storage: where to copy the data into
            /// @return   true if the current data fit into the storage
            inline constexpr bool copy_to_lazily(storage_abstract_t &storage) const
            {
                if (size > storage.max_size)
                    return false;

                // skip the actual data copy if the src pointer already is the dest pointer
                if (storage.data != data)
                    for (size_t i = 0; i < size; i++)
                        storage.data[i] = data[i];

                storage.size = size;
                return true;
            }

            /// @brief copy data into storage forcefully, without checking if it's already there
            ///
            /// @param    storage: where to copy the data into
            /// @return   true if the current data fit into the storage
            inline constexpr bool copy_to_forced(storage_abstract_t &storage) const
            {
                if (size > storage.max_size)
                    return false;
                for (size_t i = 0; i < size; i++)
                    storage.data[i] = data[i];
                storage.size = size;
                return true;
            }

            /// @brief removes the first [0]'th index element of the data array
            ///
            /// @return   uint8_t: the byte that was removed
            inline constexpr uint8_t pop_front()
            {
                --size;
                return *(data++);
            }
        };

        inline data_t consume(data_t &arg)
        {
            data_t copy(arg);
            arg.stop();
            return copy;
        }

        inline constexpr bool storage_abstract_t::append(const data_t &appended_data)
        {
            return append(appended_data.data, appended_data.size);
        }

        inline constexpr bool operator==(const data_t &lhs, const storage_abstract_t &rhs)
        {
            return lhs.data == rhs.data && lhs.size == rhs.size;
        }
        inline constexpr bool operator!=(const data_t &lhs, const storage_abstract_t &rhs)
        {
            return !(lhs == rhs);
        }
        inline constexpr bool operator==(const storage_abstract_t &lhs, const data_t &rhs)
        {
            return lhs.data == rhs.data && lhs.size == rhs.size;
        }
        inline constexpr bool operator!=(const storage_abstract_t &lhs, const data_t &rhs)
        {
            return !(lhs == rhs);
        }

        /// @brief return result type of a filter, indicating what the filter processor should do next, or if there was an error
        struct result_e
        {
            /// @brief the result_e pretends to be an enum with additional methods by using this inner enum definition along with static
            /// instances within the result_e struct itself
            enum class result_e_impl : uint8_t
            {
                CONTINUE,                 // will keep running the filter if the input is still open, otherwise will wait for a new input
                FORCE_TO_KEEP_PROCESSING, // will keep running the filter, even if the input is closed

                // NOTE: all error enums must be larger than ABORT, so that a >= ABORT check can be made to check for abort cases
                ABORT,                  // generic abort without a reason
                ABORT_EXCEEDED_STORAGE, // aborted due to not enough storage space
                ABORT_DATA_FORMAT_ERROR // aborted due to a formatting problem with the data contents in the filter
            };

            /// @brief  will keep running the filter, as long as the input is open
            static constexpr result_e_impl CONTINUE = result_e_impl::CONTINUE;

            /// @brief  will keep running the filter, even if the input is closed
            static constexpr result_e_impl FORCE_TO_KEEP_PROCESSING = result_e_impl::FORCE_TO_KEEP_PROCESSING;

            /// @brief  generic abort, which will reset
            static constexpr result_e_impl ABORT = result_e_impl::ABORT;

            /// @brief  abort due to not enough storage space
            static constexpr result_e_impl ABORT_EXCEEDED_STORAGE = result_e_impl::ABORT_EXCEEDED_STORAGE;

            /// @brief  abort due to a formatting problem with the data contents in the filter
            static constexpr result_e_impl ABORT_DATA_FORMAT_ERROR = result_e_impl::ABORT_DATA_FORMAT_ERROR;

            /// @brief returns a const char* c-string version of the enum value
            inline constexpr const char *c_str() const
            {
                switch (value)
                {
                case result_e_impl::CONTINUE:
                    return "CONTINUE";
                case result_e_impl::FORCE_TO_KEEP_PROCESSING:
                    return "FORCE_TO_KEEP_PROCESSING";
                case result_e_impl::ABORT:
                    return "ABORT";
                case result_e_impl::ABORT_EXCEEDED_STORAGE:
                    return "ABORT_EXCEEDED_STORAGE";
                case result_e_impl::ABORT_DATA_FORMAT_ERROR:
                    return "ABORT_DATA_FORMAT_ERROR";
                default:
                    return "UNKNOWN";
                }
            }

            /// @brief the underlying enum value of the filter result
            result_e_impl value = result_e_impl::FORCE_TO_KEEP_PROCESSING;

            /// @brief holds onto the name of the filter that had the error, or nullptr if there was no error
            const char *filter_with_error = nullptr;

            /// @brief returns true if the value is any ABORT* value (ABORT, ABORT_EXCEEDED_STORAGE, or ABORT_DATA_FORMAT_ERROR)
            inline constexpr bool is_aborted() const
            {
                return static_cast<uint8_t>(value) >= static_cast<uint8_t>(result_e::ABORT);
            }

            /// @brief returns the name of the last filter that returned an ABORT* enum value, or nullptr if there was no error
            inline constexpr const char *location_of_abort() const
            {
                return filter_with_error;
            }

            // boilerplate conversions and constructors
            inline constexpr result_e() = default;
            inline constexpr result_e(result_e_impl other) : value{other} {}
            inline constexpr operator uint8_t() const { return static_cast<uint8_t>(value); }
            inline constexpr operator result_e_impl() const { return value; }
        };

        /// @brief  Whether to restrict the best_output_storage() option to allow or prevent memory reuse optimizations.
        ///
        /// When using get_best_output_storage() a filter and its chain of filters can modify another storage array directly
        /// instead of always making a copy using local storage. Which can save memory copies. Such as when a single
        /// receive storage array is used as the best storage selection throughout a filter chain, meaning no extra
        /// memory copies are needed and the filters can simply modify the final target receiving byte array that is
        /// passed out of a user's receive call directly.
        /// Or when sending, only the first storage array in the chain is used and reused down the chain, isolating
        /// the user-data from modification, while still saving memory copies, since if a filter is allowed to modify
        /// the the prior storage (after it's been released by the prior filter), then operatings like simple appends,
        /// can simply append to the existing data, rather than making a new isolated deep-copy.
        enum class restrict_storage_e
        {
            /// @brief ALLOW_REUSE_OF_INPUT_MEMORY_AS_OUTPUT will allow memory reuse, which will set the following memory arrays:
            ///    input memory:              prior_output = prev filter's ouput if in middle, local_input if first rx, or initial tx if first tx
            ///    get_best_output_storage(): prior_output
            /// Use this option when your filter can modify the data array in-place, where the input memory can be completely consumed
            /// in order to use it as the next output, such as appending filters.
            ALLOW_REUSE_OF_INPUT_MEMORY_AS_OUTPUT,

            /// @brief NEVER_USE_INPUT_MEMORY_AS_OUTPUT will prevent memory reuse across the input-output boundary, which will set the following memory arrays:
            ///    input memory:              prior_output = prev filter's ouput if in middle, local_input if first rx, or initial tx if first tx
            ///    get_best_output_storage(): next_input_memory = ext filter's input if in middle, local_output if last send, or final_rx if last rx
            /// Use this option when your filter cannot modify the data array in-place, where the input memory needs to be separate from the output
            /// memory, such as SLIP encoding or segmentation of data.
            NEVER_USE_INPUT_MEMORY_AS_OUTPUT
        };

        /// @brief specifies whether the filter should restrict the input data for a filter to only be valid (data != nullptr && size > 0),
        /// or if the input is allowed to be empty (data == nullptr || size == 0).
        enum class restrict_inputs_e
        {
            ONLY_VALID,
            ALLOW_EMPTY
        };

        /// @brief Abstract class for any filter type. Used to create all of the following derived filter types:
        ///     filter::chained:    Multiple filters (of any type) chained together.
        ///     filter::base_class: A single filter defined via inheritance (a filter defined as code).
        ///     filter::functional: A single filter defined via a callable object (a filter defined as data).
        struct abstract
        {
            /// @brief abstracts the "result_e operator()(data_t &input, data_t &output)" method call
            /// so that when using the filter, error codes, chaining, and return status corrections
            /// can be handled at the process abstraction level, rather than the user-defined operator() level.
            virtual result_e process(data_t &input, data_t &output) = 0;

            /// @brief resets all member variables, and calls the user defined reset() method
            virtual void reset_all() = 0;

            virtual void select_best_storage_forward_pass(storage_abstract_t *prior)  = 0;
            virtual void select_best_storage_backwards_pass(storage_abstract_t *next) = 0;
            virtual restrict_storage_e get_forward_storage_restrictions() const       = 0;
            virtual restrict_storage_e get_backwards_storage_restrictions() const     = 0;

            /// @brief returns a reference to either the filter's local storage, or some prior or later
            /// storage in the filter chain, depending on what would reduce memory copies the most.
            /// @return the storage that the filter should be using as it's working storage
            virtual storage_abstract_t &get_best_output_storage() = 0;

            /// @brief returns a reference to the first storage array in the filter chain, that will be used
            /// first when receiving data
            /// @return the storage that the first receiving operation should place receive data into
            virtual storage_abstract_t &get_best_input_storage() = 0;

            /// @brief returns true if the filter is validly formed, such as whether a captured function is not nullptr
            /// for functional filter containers. Most filters are validated completely at compile time, and so will
            /// always return true.
            virtual bool is_valid() { return true; }

            /// @brief Get the name of the filter. Use "::name" instead for non-polymorphic constexpr name in
            /// derived classes.
            ///
            /// @return   const char*: name of filter
            virtual const char *get_name() const = 0;

            // boilerplate
            virtual ~abstract() = default;
        };

        /// @brief A filter type that chains multiple other filters together. Inherits from "filter::abstract".
        template <typename...>
        struct chained;

        /// @brief The base class for a filter type that defines a single filter. Inherits from "filter::abstract".
        ///
        /// @tparam   derived_class: The derived class type. Needed for CRTP (Curiously recurring template pattern)
        ///           so that chaining filters together doesn't need type specifications for polymorphic dynamic_casting.
        /// @tparam   max_packet_size_arg: The maximum number of bytes that the storage space will need to accommodate for the
        ///           filter, for the input or ouput, best or otherwise.
        /// @tparam   restrict_storage_arg: Whether to restrict the best_output_storage() option to allow or prevent memory reuse.
        ///              By default will ignore local storage if possible, in order to reduce memory copies.
        /// @tparam   restrict_inputs_arg: Whether to allow calling of the filter with null inputs. Defaults to skip nulls,
        ///              but can later be changed during runtime, but will reset to this templated argument's value.
        template <
            typename derived_class,
            size_t max_packet_size_arg              = default_max_packet_size,
            restrict_storage_e restrict_storage_arg = restrict_storage_e::NEVER_USE_INPUT_MEMORY_AS_OUTPUT,
            restrict_inputs_e restrict_inputs_arg   = restrict_inputs_e::ONLY_VALID>
        struct base_class : abstract
        {
            static_assert(max_packet_size_arg > 0u, "the max_packet_size needs to be > 0, and must be the maximum of the input and output packet sizes");
            virtual result_e operator()(data_t &input, data_t &output) = 0;
            virtual ~base_class()                                      = default;

            static constexpr const char *name                    = introspection::type_name<derived_class>();
            static constexpr restrict_storage_e restrict_storage = restrict_storage_arg;
            static constexpr restrict_inputs_e restrict_inputs   = restrict_inputs_arg;
            static constexpr size_t max_packet_size              = max_packet_size_arg;
            using storage_type                                   = storage_t<max_packet_size>;

            storage_type local_input_storage;
            storage_type local_output_storage;
            storage_abstract_t *best_input_storage;
            storage_abstract_t *best_output_storage;

            constexpr base_class() : local_input_storage{},
                                     local_output_storage{},
                                     best_input_storage{&local_input_storage},
                                     best_output_storage{restrict_storage_arg == restrict_storage_e::ALLOW_REUSE_OF_INPUT_MEMORY_AS_OUTPUT
                                                             ? &local_input_storage
                                                             : &local_output_storage}
            {
            }
            constexpr base_class(base_class &&other)
                : local_input_storage{std::move(other.local_input_storage)},
                  local_output_storage{std::move(other.local_output_storage)},
                  best_input_storage{&local_input_storage},
                  best_output_storage{restrict_storage_arg == restrict_storage_e::ALLOW_REUSE_OF_INPUT_MEMORY_AS_OUTPUT
                                          ? &local_input_storage
                                          : &local_output_storage}
            {
            }
            constexpr base_class(const base_class &other)
                : local_input_storage{other.local_input_storage},
                  local_output_storage{other.local_output_storage},
                  best_input_storage{&local_input_storage},
                  best_output_storage{restrict_storage_arg == restrict_storage_e::ALLOW_REUSE_OF_INPUT_MEMORY_AS_OUTPUT
                                          ? &local_input_storage
                                          : &local_output_storage}
            {
            }
            constexpr base_class &operator=(base_class &&other)
            {
                local_input_storage  = std::move(other.local_input_storage);
                local_output_storage = std::move(other.local_output_storage);
                return *this;
            }
            constexpr base_class &operator=(const base_class other)
            {
                local_input_storage  = other.local_input_storage;
                local_output_storage = other.local_output_storage;
                return *this;
            }

            const char *get_name() const final
            {
                return name;
            }

            result_e process(data_t &input, data_t &output) final
            {
                if constexpr (restrict_inputs == restrict_inputs_e::ONLY_VALID)
                {
                    if (!input)
                        return result_e::CONTINUE;
                }
                if (output)
                    return result_e::CONTINUE;
#ifdef CPPTXRX_TRACE_FILTER_PRE_PROCESS
                CPPTXRX_TRACE_FILTER_PRE_PROCESS();
#endif
                result_e r = (*this)(input, output);
#ifdef CPPTXRX_TRACE_FILTER_POST_PROCESS
                CPPTXRX_TRACE_FILTER_POST_PROCESS();
#endif
                if (r.is_aborted())
                    r.filter_with_error = name;
                else if (input) // if the input (input) is still active after the filter, force the filter to keep processing it, even if the filter commanded CONTINUE
                    r = result_e::FORCE_TO_KEEP_PROCESSING;
                return r;
            }
            storage_abstract_t &get_best_output_storage() final
            {
                return *best_output_storage;
            }
            storage_abstract_t &get_best_input_storage() final
            {
                return *best_input_storage;
            }
            virtual void reset() = 0;
            void reset_all() final
            {
                reset();
                local_input_storage.reset();
                local_output_storage.reset();
            }
            template <typename T2>
            chained<derived_class, typename std::remove_const<T2>::type> chain(T2 &&next_filter)
            {
                return chained<derived_class, typename std::remove_const<T2>::type>(*dynamic_cast<derived_class *>(this), std::forward<T2>(next_filter));
            }
            template <typename T2>
            chained<derived_class, typename std::remove_const<T2>::type> then(T2 &&next_filter)
            {
                return chained<derived_class, typename std::remove_const<T2>::type>(*dynamic_cast<derived_class *>(this), std::forward<T2>(next_filter));
            }
            void select_best_storage_forward_pass(storage_abstract_t *last_node_output) final
            {
                if constexpr (restrict_storage == restrict_storage_e::ALLOW_REUSE_OF_INPUT_MEMORY_AS_OUTPUT)
                {
                    if (last_node_output != nullptr && last_node_output->max_size >= max_packet_size)
                    {
                        best_input_storage  = last_node_output;
                        best_output_storage = last_node_output;
                    }
                    else
                    {
                        best_input_storage  = &local_input_storage;
                        best_output_storage = &local_input_storage;
                    }
                }

                if constexpr (restrict_storage == restrict_storage_e::NEVER_USE_INPUT_MEMORY_AS_OUTPUT)
                {
                    if (last_node_output != nullptr && last_node_output->max_size >= max_packet_size)
                    {
                        best_input_storage  = last_node_output;
                        best_output_storage = &local_output_storage;
                    }
                    else
                    {
                        best_input_storage  = &local_input_storage;
                        best_output_storage = &local_output_storage;
                    }
                }
            }
            void select_best_storage_backwards_pass(storage_abstract_t *next) final
            {
                // NOTE: no (next->max_size >= max_packet_size) check is done here, so that smaller receive() calls can still succeed
                // when possible - if the final receive output is large enough even though it may not be the max size. This is okay,
                // since a ABORT_EXCEEDED_STORAGE error will still be raised if the receive wasn't large enough, just earlier in the
                // chain.
                if (next != nullptr)
                {
                    best_output_storage = next;
                    if constexpr (restrict_storage == restrict_storage_e::ALLOW_REUSE_OF_INPUT_MEMORY_AS_OUTPUT)
                        best_input_storage = next;
                }
            }
            restrict_storage_e get_forward_storage_restrictions() const final
            {
                return restrict_storage_arg;
            }
            restrict_storage_e get_backwards_storage_restrictions() const final
            {
                return restrict_storage_arg;
            }
        };

        template <typename F1>
        struct chained<F1>
        {
            using type = F1;
        };

        template <typename F1, typename F2, typename... F3>
        struct chained<F1, F2, F3...> final : abstract
        {
            using type                                         = chained<F1, F2, F3...>;
            static constexpr const char *name                  = introspection::type_name<type>();
            static constexpr restrict_inputs_e restrict_inputs = restrict_inputs_e::ALLOW_EMPTY;

            template <typename T2>
            chained<type, typename std::remove_const<T2>::type> chain(T2 &&next_filter)
            {
                return chained<type, typename std::remove_const<T2>::type>(*this, std::forward<T2>(next_filter));
            }
            template <typename T2>
            chained<type, typename std::remove_const<T2>::type> then(T2 &&next_filter)
            {
                return chained<type, typename std::remove_const<T2>::type>(*this, std::forward<T2>(next_filter));
            }

            F1 f1;
            typename chained<F2, F3...>::type f2;
            data_t mid;
            result_e f1_result;
            result_e f2_result;

            // allow argument passing
            template <typename... Fs>
            chained(F1 &&init1, Fs &&...inits) : f1(std::move(init1)),
                                                 f2(std::forward<Fs>(inits)...),
                                                 mid(),
                                                 f1_result{result_e::CONTINUE},
                                                 f2_result{result_e::CONTINUE}
            {
                select_best_storage_forward_pass(nullptr);
            }
            template <typename... Fs>
            chained(const F1 &init1, Fs &&...inits) : f1(init1),
                                                      f2(std::forward<Fs>(inits)...),
                                                      mid(),
                                                      f1_result{result_e::CONTINUE},
                                                      f2_result{result_e::CONTINUE}
            {
                select_best_storage_forward_pass(nullptr);
            }

            // all default constructors
            chained() : f1(),
                        f2(),
                        mid(),
                        f1_result{result_e::CONTINUE},
                        f2_result{result_e::CONTINUE}
            {
                select_best_storage_forward_pass(nullptr);
            }

            const char *get_name() const final
            {
                return name;
            }

            void reset_all() final
            {
                f1.reset_all();
                f2.reset_all();
                mid.stop();
                f1_result = result_e::CONTINUE;
                f2_result = result_e::CONTINUE;
            }

            using storage_type = typename F1::storage_type;
            storage_abstract_t &get_best_output_storage() final
            {
                return f2.get_best_output_storage();
            }
            storage_abstract_t &get_best_input_storage() final
            {
                return f1.get_best_input_storage();
            }
            void select_best_storage_forward_pass(storage_abstract_t *prior) final
            {
                f1.select_best_storage_forward_pass(prior);
                f2.select_best_storage_forward_pass(&f1.get_best_output_storage());
            }
            void select_best_storage_backwards_pass(storage_abstract_t *next) final
            {
                f2.select_best_storage_backwards_pass(next);
                if (&f2.get_best_input_storage() == next)
                    f1.select_best_storage_backwards_pass(next);
            }
            restrict_storage_e get_forward_storage_restrictions() const final
            {
                return f1.get_forward_storage_restrictions();
            }
            restrict_storage_e get_backwards_storage_restrictions() const final
            {
                return f2.get_backwards_storage_restrictions();
            }
            result_e process(data_t &input, data_t &output) final
            {
                if ( // don't allow a new mid (input for f2) to be created, if f2 has said
                    f2_result != result_e::FORCE_TO_KEEP_PROCESSING &&

                    // only run the filter if it's prior output (mid) has finished processing, and the input type is allowed
                    !mid && (f1.restrict_inputs == restrict_inputs_e::ALLOW_EMPTY || input || f1_result == result_e::FORCE_TO_KEEP_PROCESSING))
                {
                    f1_result = f1.process(input, mid);

                    // any abort needs to flow up the chained
                    if (f1_result.is_aborted())
                        return f1_result;
                }
                if (
                    // only run the filter if it's prior output (output) has finished processing, and the input type is allowed
                    !output && (f2.restrict_inputs == restrict_inputs_e::ALLOW_EMPTY || mid || f2_result == result_e::FORCE_TO_KEEP_PROCESSING))
                {
                    f2_result = f2.process(mid, output);

                    // any abort needs to flow up the chained
                    if (f2_result.is_aborted())
                        return f2_result;
                }

                // propagate a "keep processing" instruction up the chained
                if (f1_result == result_e::FORCE_TO_KEEP_PROCESSING ||
                    f2_result == result_e::FORCE_TO_KEEP_PROCESSING)
                    return result_e::FORCE_TO_KEEP_PROCESSING;

                return result_e::CONTINUE;
            };
        };

        /// @brief Lets you define the function as data (a lambda, or std::function, etc.), instead of defining the filter
        //         operator()(...) in code by inheriting from filter::base_class
        ///
        /// @tparam   max_packet_size: The maximum number of bytes that the storage space will need to accommodate for the
        ///           filter, best or otherwise.
        /// @tparam   restrict_storage_arg: Whether to restrict the best_output_storage() option to allow or prevent memory reuse.
        ///              By default will ignore local storage if possible, in order to reduce memory copies.
        /// @tparam   restrict_inputs_arg: Whether to allow calling of the filter with null inputs. Defaults to skip nulls,
        ///              but can later be changed during runtime, but will reset to this templated argument's value.
        /// @tparam   T: The input function type, which will automatically resolve to the raw function type (such as the lambda type)
        ///              to avoid abstraction overhead, though the type can be a std::function if you don't care about the overhead.
        template <size_t max_packet_size                  = default_max_packet_size,
                  restrict_storage_e restrict_storage_arg = restrict_storage_e::NEVER_USE_INPUT_MEMORY_AS_OUTPUT,
                  restrict_inputs_e restrict_inputs_arg   = restrict_inputs_e::ONLY_VALID,
                  typename T                              = int>
        struct functional final : base_class<functional<max_packet_size, restrict_storage_arg, restrict_inputs_arg, T>, max_packet_size, restrict_storage_arg, restrict_inputs_arg>
        {
            T func;
            functional(T &&func_arg) : func{std::move(func_arg)} {}
            functional(const T &func_arg) : func{func_arg} {}
            result_e operator()(data_t &input, data_t &output) final
            {
                return func(this->get_best_output_storage(), input, output);
            };
            void reset() final {}
            bool is_valid() final
            {
                if constexpr (std::is_pointer<T>::value)
                    return func != nullptr;
                return true;
            }
        };

        /// @brief "filter::create()" can turning lambdas and other functional/callable objects into a data filter.
        //
        // NOTE: Creating a new filter via the long way of inheriting from filter::base_class instead
        //       of using "filter::create()" can sometimes be slightly more efficient, since it can avoid
        //       the extra functional abstraction if you use the"final" keyword to let the compiler remove virtualization".
        //
        // Example:
        //     using namespace interface;
        //     auto filter_that_appends_captured_value = filter::create(
        //         [&](filter::storage_abstract_t &storage, filter::data_t &input, filter::data_t &output)
        //         {
        //             if (!input.copy_to_lazily(storage))
        //                 return filter::result_e::ABORT_EXCEEDED_STORAGE;
        //             if (!storage.append(captured_value))
        //                 return filter::result_e::ABORT_EXCEEDED_STORAGE;
        //             output.start(storage);
        //             input.stop();
        //             return filter::result_e::CONTINUE;
        //         });
        ///
        /// @tparam   max_packet_size: The maximum number of bytes that the storage space will need to accommodate for the
        ///           filter, best or otherwise.
        /// @tparam   restrict_storage_arg: Whether to restrict the best_output_storage() option to allow or prevent memory reuse.
        ///              By default will ignore local storage if possible, in order to reduce memory copies.
        /// @tparam   restrict_inputs_arg: Whether to allow calling of the filter with null inputs. Defaults to skip nulls,
        ///              but can later be changed during runtime, but will reset to this templated argument's value.
        /// @tparam   T: The input function type, which will automatically resolve to the raw function type (such as the lambda type)
        ///              to avoid abstraction overhead, though the type can be a std::function if you don't care about the overhead.
        /// @param    func: the function you'd like to use to define the filter
        /// @return   filter::functional<max_packet_size, restrict_storage_arg, restrict_inputs_arg, T>: the created data filter
        template <size_t max_packet_size                          = default_max_packet_size,
                  filter::restrict_storage_e restrict_storage_arg = filter::restrict_storage_e::NEVER_USE_INPUT_MEMORY_AS_OUTPUT,
                  filter::restrict_inputs_e restrict_inputs_arg   = filter::restrict_inputs_e::ONLY_VALID,
                  typename T                                      = int>
        filter::functional<
            max_packet_size,
            restrict_storage_arg,
            restrict_inputs_arg,
            typename std::remove_const<std::remove_reference_t<T>>::type>
        create(T &&func)
        {
            return {std::forward<T>(func)};
        }

    } // namespace filter

} // namespace interface

#endif // CPPTXRX_FILTER_H_