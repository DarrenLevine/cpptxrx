/// @file cpptxrx_filters.h
/// @author Darren V Levine (DarrenVLevine@gmail.com)
/// @brief several useful data filter class implementations
///
/// @copyright (c) 2024 Darren V Levine. This code is licensed under MIT license (see LICENSE file for details).
///
#ifndef CPPTXRX_FILTERS_H_
#define CPPTXRX_FILTERS_H_

#include "cpptxrx_filter.h"

namespace interface
{
    namespace filters
    {
        template <size_t max_size = default_max_packet_size>
        struct forward_by_copy final : filter::base_class<forward_by_copy<max_size>, max_size, filter::restrict_storage_e::NEVER_USE_INPUT_MEMORY_AS_OUTPUT>
        {
            filter::result_e operator()(filter::data_t &input, filter::data_t &output) final
            {
                filter::storage_abstract_t &storage = this->get_best_output_storage();
                if (!input.copy_to_lazily(storage))
                    return filter::result_e::ABORT_EXCEEDED_STORAGE;
                output.start_and_consume(storage);
                input.stop();
                return filter::result_e::CONTINUE;
            };
            void reset() final {}
            ~forward_by_copy() final = default;
        };

        template <size_t max_size = default_max_packet_size>
        struct forward_by_pointing final : filter::base_class<forward_by_pointing<max_size>, max_size, filter::restrict_storage_e::ALLOW_REUSE_OF_INPUT_MEMORY_AS_OUTPUT>
        {
            inline filter::result_e operator()(filter::data_t &input, filter::data_t &output) final
            {
                input.pass_to(output);
                return filter::result_e::CONTINUE;
            };
            inline void reset() final {}
            ~forward_by_pointing() final = default;
        };

        template <uint8_t... data>
        struct bytes
        {
            static constexpr uint8_t values[sizeof...(data)] = {data...};
        };

        /// @brief append data from a compile time type (use filters::bytes<> or use it as an example for your own custom class)
        ///
        /// @tparam   data_to_append: a data type that specifies what to append (see "struct bytes")
        /// @tparam   max_size: the maximum size of a data packet to filter
        template <typename data_to_append, size_t max_size = default_max_packet_size>
        struct type_append final : filter::base_class<type_append<data_to_append, max_size>, max_size, filter::restrict_storage_e::ALLOW_REUSE_OF_INPUT_MEMORY_AS_OUTPUT>
        {
            filter::result_e operator()(filter::data_t &input, filter::data_t &output) final
            {
                filter::storage_abstract_t &storage = this->get_best_output_storage();

                // only copy the existing data into storage, it it isn't already in storage (a.k.a. when the input is the storage)
                if (!input.copy_to_lazily(storage))
                    return filter::result_e::ABORT_EXCEEDED_STORAGE;

                // append some extra user-supplied data
                if (!storage.append(data_to_append::values, sizeof(data_to_append::values) / sizeof(data_to_append::values[0])))
                    return filter::result_e::ABORT_EXCEEDED_STORAGE;

                // send it all to the output
                output.start_and_consume(storage);

                // mark the input as finished, so that the caller knows it's no longer needed by future calls
                input.stop();
                return filter::result_e::CONTINUE;
            };
            void reset() final {}
            ~type_append() final = default;
        };

        /// @brief append data from a compile-time or runtime data array
        ///
        /// @tparam   max_append_size: the maximum size of a data to append
        /// @tparam   max_size: the maximum size of a data packet to filter
        template <size_t max_append_size, size_t max_size = default_max_packet_size>
        struct data_append final : filter::base_class<data_append<max_append_size, max_size>, max_size, filter::restrict_storage_e::ALLOW_REUSE_OF_INPUT_MEMORY_AS_OUTPUT>
        {
            uint8_t appended_data[max_append_size]         = {};
            size_t appended_data_size                      = 0;
            static constexpr size_t appended_data_max_size = max_append_size;

            constexpr data_append(const data_append &other)
                : filter::base_class<data_append<max_append_size, max_size>, max_size, filter::restrict_storage_e::ALLOW_REUSE_OF_INPUT_MEMORY_AS_OUTPUT>()
            {
                for (size_t i = 0; i < other.appended_data_size; i++)
                    appended_data[i] = other.appended_data[i];
                appended_data_size = other.appended_data_size;
            }
            constexpr data_append(data_append<max_append_size, max_size> &&other) : data_append(other) {}
            constexpr data_append &operator=(const data_append<max_append_size, max_size> &other)
            {
                for (size_t i = 0; i < other.appended_data_size; i++)
                    appended_data[i] = other.appended_data[i];
                appended_data_size = other.appended_data_size;
                return *this;
            }
            constexpr data_append &operator=(data_append<max_append_size, max_size> &&other)
            {
                *this = other;
                return *this;
            }

            constexpr data_append(const uint8_t *data, size_t N)
                : filter::base_class<data_append<max_append_size, max_size>, max_size, filter::restrict_storage_e::ALLOW_REUSE_OF_INPUT_MEMORY_AS_OUTPUT>()
            {
                appended_data_size = 0;
                if (data != nullptr)
                {
                    if (N > max_append_size)
                        N = max_append_size;
                    for (; appended_data_size < N; appended_data_size++)
                        appended_data[appended_data_size] = data[appended_data_size];
                }
            }
            template <size_t N>
            constexpr data_append(const uint8_t (&data)[N])
                : data_append(data, N) {}

            constexpr data_append(const char *data)
                : filter::base_class<data_append<max_append_size, max_size>, max_size, filter::restrict_storage_e::ALLOW_REUSE_OF_INPUT_MEMORY_AS_OUTPUT>()
            {
                appended_data_size = 0;
                if (data != nullptr)
                {
                    for (; appended_data_size < max_append_size && data[appended_data_size] != 0; appended_data_size++)
                        appended_data[appended_data_size] = data[appended_data_size];
                }
            }
            template <size_t N>
            constexpr data_append(const char (&data)[N])
                : data_append{reinterpret_cast<const uint8_t *>(data), N - 1u * (N > 0)} {}
            constexpr data_append(const char *data, size_t N)
                : data_append(data, N) {}

            filter::result_e operator()(filter::data_t &input, filter::data_t &output) final
            {
                filter::storage_abstract_t &storage = this->get_best_output_storage();

                if (!input.copy_to_lazily(storage))
                    return filter::result_e::ABORT_EXCEEDED_STORAGE;

                if (!storage.append(reinterpret_cast<const uint8_t *>(appended_data), appended_data_size))
                    return filter::result_e::ABORT_EXCEEDED_STORAGE;

                output.start_and_consume(storage);
                input.stop();
                return filter::result_e::CONTINUE;
            };
            void reset() final {}
            bool is_valid() final
            {
                return appended_data_size > 0u;
            }
            ~data_append() final = default;
        };

        template <size_t max_size = default_max_packet_size, size_t N = 1, typename T = uint8_t>
        data_append<N, max_size> append(T (&&data)[N])
        {
            return {data};
        }
        template <size_t max_size = default_max_packet_size, size_t N = 1, typename T = const uint8_t>
        data_append<N, max_size> append(const T (&data)[N])
        {
            return {data};
        }

        /// @brief Either splits one operation (send or receive) into many (if fixed_size < op_size), or combines multiple operations into one (if fixed_size > op_size)
        ///
        /// @tparam   fixed_size: the fixed size to output
        /// @tparam   max_size: the maximum packet size to process
        template <size_t fixed_size, size_t max_size = default_max_packet_size>
        struct enforce_fixed_size final : filter::base_class<enforce_fixed_size<fixed_size, max_size>, max_size, filter::restrict_storage_e::NEVER_USE_INPUT_MEMORY_AS_OUTPUT>
        {
            static_assert(fixed_size <= max_size, "fixed_size must be smaller or equal to max_size");
            filter::result_e operator()(filter::data_t &input, filter::data_t &output) final
            {
                filter::storage_abstract_t &storage = this->get_best_output_storage();
                while (input.size > 0u && storage.size < fixed_size)
                {
                    storage.data[storage.size++] = *(input.data++);
                    --input.size;
                }
                if (storage.size >= fixed_size)
                    output.start_and_consume(storage);
                return filter::result_e::CONTINUE;
            };
            void reset() final {}
            ~enforce_fixed_size() final = default;
        };

        /// @brief if the input data size is larger than the upper_limit, the data will get split up into <= upper_limit sized segments
        /// remainder data from the split, and data smaller or equal in size to the upper_limit will not get split
        ///
        /// @tparam   upper_limit: the upper limit size to segment outputs to when exceeded
        /// @tparam   max_size: the maximum packet size to process
        template <size_t upper_limit, size_t max_size = default_max_packet_size>
        struct split_if_larger final : filter::base_class<split_if_larger<upper_limit, max_size>, max_size, filter::restrict_storage_e::NEVER_USE_INPUT_MEMORY_AS_OUTPUT>
        {
            static_assert(upper_limit <= max_size, "upper_limit must be smaller or equal to max_size");
            filter::result_e operator()(filter::data_t &input, filter::data_t &output) final
            {
                filter::storage_abstract_t &storage = this->get_best_output_storage();
                while (input.size > 0u && storage.size < upper_limit)
                {
                    storage.data[storage.size++] = *(input.data++);
                    --input.size;
                }
                if (storage)
                    output.start_and_consume(storage);
                return filter::result_e::CONTINUE;
            };
            void reset() final {}
            ~split_if_larger() final = default;
        };

        /// @brief aggregates data until a type specified delimiter(s) are found, and then outputs all the data before the delimiter(s)
        ///
        /// @tparam   delimiter_type: a type containing a byte array of delimiters called "values" (see "struct bytes")
        /// @tparam   max_size: max size of pre-allocated storage
        template <typename delimiter_type, size_t max_size = default_max_packet_size>
        struct type_delimit final : filter::base_class<type_delimit<delimiter_type, max_size>, max_size, filter::restrict_storage_e::NEVER_USE_INPUT_MEMORY_AS_OUTPUT>
        {
            static constexpr size_t delimiter_array_size           = sizeof(delimiter_type::values) / sizeof(delimiter_type::values[0]);
            static constexpr size_t delimiter_array_size_minus_one = delimiter_array_size - 1u;
            static_assert(delimiter_array_size, "must provide at least one delimiter byte");

            size_t matched_delimiter_elements = 0;

            filter::result_e operator()(filter::data_t &input, filter::data_t &output) final
            {
                filter::storage_abstract_t &storage = this->get_best_output_storage();
                while (input)
                {
                    const uint8_t next_byte = input.pop_front();

                    // if we're currently matching the delimiter
                    if (next_byte == delimiter_type::values[matched_delimiter_elements])
                    {
                        // increment matched_delimiter_elements, and if it's maxed out
                        if (++matched_delimiter_elements >= delimiter_array_size)
                        {
                            // reset the matching count
                            matched_delimiter_elements = 0u;

                            // remove the added delimiter bytes (num-1), now that it's confirmed they all match:
                            storage.size -= delimiter_array_size_minus_one;

                            // push the storage into the output
                            output.start_and_consume(storage);
                            return filter::result_e::CONTINUE;
                        }
                    }
                    else
                    {
                        matched_delimiter_elements = 0u;
                    }

                    // append the next byte, even if it's a delimiter match, since it could just be a partial
                    // match that is actually data, plus it's easy to remove later if it's a real delimiter
                    if (!storage.append(next_byte))
                        return filter::result_e::ABORT_EXCEEDED_STORAGE;
                }
                return filter::result_e::CONTINUE;
            };
            void reset() final
            {
                matched_delimiter_elements = 0u;
            }
            ~type_delimit() final = default;
        };

        /// @brief aggregates data until a data specified delimiter(s) are found, and then outputs all the data before the delimiter(s)
        ///
        /// @tparam   max_delimiter_size: max size of delimiters
        /// @tparam   max_size: max size of pre-allocated storage
        template <size_t max_delimiter_size, size_t max_size = default_max_packet_size>
        struct data_delimit final : filter::base_class<data_delimit<max_delimiter_size, max_size>, max_size, filter::restrict_storage_e::NEVER_USE_INPUT_MEMORY_AS_OUTPUT>
        {
            static_assert(max_delimiter_size > 0, "must provide at least one delimiter byte");

            uint8_t delimiter_array[max_delimiter_size]      = {};
            size_t delimiter_array_size                      = 0u;
            size_t matched_delimiter_elements                = 0;
            static constexpr size_t delimiter_array_max_size = max_delimiter_size;

            constexpr data_delimit(const data_delimit &other)
                : filter::base_class<data_delimit<max_delimiter_size, max_size>, max_size, filter::restrict_storage_e::NEVER_USE_INPUT_MEMORY_AS_OUTPUT>()
            {
                for (size_t i = 0; i < other.delimiter_array_size; i++)
                    delimiter_array[i] = other.delimiter_array[i];
                delimiter_array_size = other.delimiter_array_size;
            }
            constexpr data_delimit(data_delimit<max_delimiter_size, max_size> &&other) : data_delimit(other) {}
            constexpr data_delimit &operator=(const data_delimit<max_delimiter_size, max_size> &other)
            {
                for (size_t i = 0; i < other.delimiter_array_size; i++)
                    delimiter_array[i] = other.delimiter_array[i];
                delimiter_array_size = other.delimiter_array_size;
                return *this;
            }
            constexpr data_delimit &operator=(data_delimit<max_delimiter_size, max_size> &&other)
            {
                *this = other;
                return *this;
            }

            constexpr data_delimit(const uint8_t *data, size_t N)
                : filter::base_class<data_delimit<max_delimiter_size, max_size>, max_size, filter::restrict_storage_e::NEVER_USE_INPUT_MEMORY_AS_OUTPUT>()
            {
                delimiter_array_size = 0;
                if (data != nullptr)
                {
                    if (N > max_delimiter_size)
                        N = max_delimiter_size;
                    for (; delimiter_array_size < N; delimiter_array_size++)
                        delimiter_array[delimiter_array_size] = data[delimiter_array_size];
                }
            }
            template <size_t N>
            constexpr data_delimit(const uint8_t (&data)[N])
                : data_delimit(data, N) {}

            constexpr data_delimit(const char *data)
                : filter::base_class<data_delimit<max_delimiter_size, max_size>, max_size, filter::restrict_storage_e::NEVER_USE_INPUT_MEMORY_AS_OUTPUT>()
            {
                delimiter_array_size = 0;
                if (data != nullptr)
                {
                    for (; delimiter_array_size < max_delimiter_size && data[delimiter_array_size] != 0; delimiter_array_size++)
                        delimiter_array[delimiter_array_size] = data[delimiter_array_size];
                }
            }
            template <size_t N>
            constexpr data_delimit(const char (&data)[N])
                : data_delimit{reinterpret_cast<const uint8_t *>(data), N - 1u * (N > 0)} {}
            constexpr data_delimit(const char *data, size_t N)
                : data_delimit(data, N) {}

            filter::result_e operator()(filter::data_t &input, filter::data_t &output) final
            {
                filter::storage_abstract_t &storage = this->get_best_output_storage();
                while (input)
                {
                    const uint8_t next_byte = input.pop_front();

                    // if we're currently matching the delimiter
                    if (next_byte == delimiter_array[matched_delimiter_elements])
                    {
                        // increment matched_delimiter_elements, and if it's maxed out
                        if (++matched_delimiter_elements >= delimiter_array_size)
                        {
                            // reset the matching count
                            matched_delimiter_elements = 0u;

                            // remove the added delimiter bytes (num-1), now that it's confirmed they all match:
                            storage.size -= delimiter_array_size - 1u;

                            // push the storage into the output
                            output.start_and_consume(storage);
                            return filter::result_e::CONTINUE;
                        }
                    }
                    else
                    {
                        matched_delimiter_elements = 0u;
                    }

                    // append the next byte, even if it's a delimiter match, since it could just be a partial
                    // match that is actually data, plus it's easy to remove later if it's a real delimiter
                    if (!storage.append(next_byte))
                        return filter::result_e::ABORT_EXCEEDED_STORAGE;
                }
                return filter::result_e::CONTINUE;
            };
            void reset() final
            {
                matched_delimiter_elements = 0u;
            }
            ~data_delimit() final = default;
        };

        template <size_t max_size = default_max_packet_size, size_t N = 1, typename T = uint8_t>
        data_delimit<N, max_size> delimit(T (&&data)[N])
        {
            return {data};
        }
        template <size_t max_size = default_max_packet_size, size_t N = 1, typename T = const uint8_t>
        data_delimit<N, max_size> delimit(const T (&data)[N])
        {
            return {data};
        }

        template <size_t max_size = default_max_packet_size>
        struct repeat final : filter::base_class<repeat<max_size>, max_size, filter::restrict_storage_e::NEVER_USE_INPUT_MEMORY_AS_OUTPUT>
        {
            const size_t number_of_repeats;
            constexpr repeat(size_t N_repeats) : number_of_repeats{N_repeats} {}
            size_t send_counter = 0;
            filter::result_e operator()(filter::data_t &input, filter::data_t &output) final
            {
                output.start(input.data, input.size);
                if (++send_counter >= number_of_repeats)
                {
                    input.stop();
                    send_counter = 0;
                }
                return filter::result_e::CONTINUE;
            };
            void reset() final
            {
                send_counter = 0;
            }
            ~repeat() final = default;
        };

        namespace slip
        {
            static constexpr uint8_t frame_end               = 0xC0;
            static constexpr uint8_t frame_escape            = 0xDB;
            static constexpr uint8_t transposed_frame_end    = 0xDC;
            static constexpr uint8_t transposed_frame_escape = 0xDD;

            /// @brief whether to add an extra frame end to the start of a message. The YES option can guarantee that the receiver has
            /// enough information to prevent partial data from being interpreted as a valid packet, even after hw resets.
            /// NOTE: YES is nonstandard - however, if the receiving decoder is set to "wait_for_first_frame_end_e::YES", then setting
            /// the encoder "prefix_with_frame_end_e::YES" is also recommended so that the first packet isn't dropped.
            enum class prefix_with_frame_end_e
            {
                YES,
                NO
            };

            template <size_t max_size = default_max_packet_size, prefix_with_frame_end_e prefix_with_frame_end = prefix_with_frame_end_e::NO>
            struct encode final : filter::base_class<encode<max_size, prefix_with_frame_end>, max_size, filter::restrict_storage_e::NEVER_USE_INPUT_MEMORY_AS_OUTPUT>
            {
                filter::result_e operator()(filter::data_t &input, filter::data_t &output) final
                {
                    auto &storage               = this->get_best_output_storage();
                    size_t minimum_encoded_size = input.size + 1 + (prefix_with_frame_end == prefix_with_frame_end_e::YES); // input + up to 2 frame_ends
                    if (minimum_encoded_size > storage.max_size)
                        return filter::result_e::ABORT_EXCEEDED_STORAGE;

                    storage.size = 0;

                    if constexpr (prefix_with_frame_end == prefix_with_frame_end_e::YES)
                        storage.data[storage.size++] = frame_end;

                    for (size_t i = 0; i < input.size; i++)
                    {
                        auto element = input.data[i];
                        if (element == frame_end)
                        {
                            // only recheck minimum_encoded_size if it grows
                            if (++minimum_encoded_size > storage.max_size)
                                return filter::result_e::ABORT_EXCEEDED_STORAGE;
                            storage.data[storage.size++] = frame_escape;
                            storage.data[storage.size++] = transposed_frame_end;
                        }
                        else if (element == frame_escape)
                        {
                            // only recheck minimum_encoded_size if it grows
                            if (++minimum_encoded_size > storage.max_size)
                                return filter::result_e::ABORT_EXCEEDED_STORAGE;
                            storage.data[storage.size++] = frame_escape;
                            storage.data[storage.size++] = transposed_frame_escape;
                        }
                        else
                            storage.data[storage.size++] = element;
                    }
                    storage.data[storage.size++] = frame_end;
                    output.start_and_consume(storage);
                    input.stop();
                    return filter::result_e::CONTINUE;
                };
                void reset() final {}
                ~encode() final = default;
            };

            /// @brief when first receiving data, whether to throw out all data before the first frame end,
            /// The YES option can guarantee that no partial data is received first, such as might occur during hw resets.
            /// NOTE: YES is nonstandard - however, if the sending encoder is set to "prefix_with_frame_end_e::YES", then setting
            /// the decoder to "wait_for_first_frame_end_e::YES" is also recommended so that the first packet isn't dropped.
            enum class wait_for_first_frame_end_e
            {
                YES,
                NO
            };

            template <size_t max_size = default_max_packet_size, wait_for_first_frame_end_e wait_for_first_frame_end = wait_for_first_frame_end_e::NO>
            struct decode final : filter::base_class<decode<max_size, wait_for_first_frame_end>, max_size, filter::restrict_storage_e::NEVER_USE_INPUT_MEMORY_AS_OUTPUT>
            {
                bool in_escape            = false;
                bool need_first_frame_end = wait_for_first_frame_end == wait_for_first_frame_end_e::YES;
                filter::result_e operator()(filter::data_t &input, filter::data_t &output) final
                {
                    filter::storage_abstract_t &storage = this->get_best_output_storage();
                    while (input)
                    {
                        const uint8_t element = input.pop_front();

                        // don't allow the processing of any data before the first frame end if wait_for_first_frame_end is true
                        // ensuring no corrupt data can be allowed through if the data line can start with partial data
                        if (need_first_frame_end)
                        {
                            need_first_frame_end = element != frame_end;
                            continue;
                        }

                        if (in_escape)
                        {
                            in_escape = false;
                            if (element == transposed_frame_end)
                            {
                                if (!storage.append(frame_end))
                                    return filter::result_e::ABORT_EXCEEDED_STORAGE;
                            }
                            else if (element == transposed_frame_escape)
                            {
                                if (!storage.append(frame_escape))
                                    return filter::result_e::ABORT_EXCEEDED_STORAGE;
                            }
                            else
                            {
                                return filter::result_e::ABORT_DATA_FORMAT_ERROR;
                            }
                        }
                        else if (element == frame_end)
                        {
                            if (storage.size > 0u)
                            {
                                output.start_and_consume(storage);
                                return filter::result_e::CONTINUE;
                            }
                        }
                        else if (element == frame_escape)
                        {
                            in_escape = true;
                        }
                        else if (!storage.append(element))
                            return filter::result_e::ABORT_EXCEEDED_STORAGE;
                    }
                    return filter::result_e::CONTINUE;
                };
                void reset() final
                {
                    in_escape            = false;
                    need_first_frame_end = wait_for_first_frame_end == wait_for_first_frame_end_e::YES;
                }
                ~decode() final = default;
            };

        } // namespace slip

    } // namespace filters

} // namespace interface

#endif // CPPTXRX_FILTERS_H_