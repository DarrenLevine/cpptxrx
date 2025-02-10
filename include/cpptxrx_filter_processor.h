/// @file cpptxrx_filter_processor.h
/// @author Darren V Levine (DarrenVLevine@gmail.com)
/// @brief implements filter state tracking and processing used in the cpptxrx_factory code
///
/// @copyright (c) 2024 Darren V Levine. This code is licensed under MIT license (see LICENSE file for details).
///
#ifndef CPPTXRX_FILTER_PROCESSOR_H_
#define CPPTXRX_FILTER_PROCESSOR_H_

#include "cpptxrx_op_types.h"

namespace interface
{
    template <bool is_sending>
    class filter_processor
    {
        struct ext_op_completed
        {
            constexpr explicit ext_op_completed(bool arg) noexcept : value{arg} {}
            constexpr operator bool() const noexcept
            {
                return value;
            }

        private:
            bool value = false;
        };

    public:
        constexpr operator bool() const noexcept
        {
            return p_filter != nullptr;
        }
        constexpr void replace_filter(const std::shared_ptr<filter::abstract> &new_p_filter) noexcept
        {
            p_filter = new_p_filter;
            reset_all();
        }
        template <bool is_sending2 = is_sending, typename = typename std::enable_if_t<is_sending2>>
        constexpr ext_op_completed process_sending_filter(send_op *&ext_p_send_op, send_op *&p_send_op)
        {
            // without a filter or external operation, the filter should not run
            if (p_filter == nullptr || ext_p_send_op == nullptr)
                return ext_op_completed(false);

            // wait for the internal send to finish, or reset if it is finished
            if (p_send_op != nullptr)
            {
                if (p_send_op->is_operating())
                    return ext_op_completed(false);

                // handle any errors
                if (p_send_op->status != status_e::SUCCESS)
                {
                    ext_p_send_op->status = p_send_op->status;
                    ext_p_send_op         = nullptr;
                    p_send_op             = nullptr;
                    reset_all();
                    return ext_op_completed(true);
                }

                output_data.stop();
                p_send_op = nullptr;
            }

            // consume the ext_p_send_op into the input_data if needed
            if (ext_p_send_op->status == status_e::START_NEW_OP && !input_data)
            {
                flip_status_from_started_to_in_progress(ext_p_send_op);
                input_data.start(ext_p_send_op->send_data, ext_p_send_op->send_size);
            }

            // see if the filter can yield a new output, has an error, or is done until next input
            const yield_e yield_status = yield_next();

            if (yield_status == yield_e::YIELD_ONE_OUTPUT)
            {
                auto &output_storage = p_filter->get_best_output_storage();
                if (output_data.data == output_storage.data && output_storage.size > 0u)
                {
                    last_error_location = p_filter->get_name();
                    ext_p_send_op->status.set_error_with_additional_info(
                        status_e::FILTER_STORAGE_NOT_CONSUMED,
                        reinterpret_cast<const char *&&>(last_error_location));
                    ext_p_send_op = nullptr;
                    p_send_op     = nullptr;
                    reset_all();
                    return ext_op_completed(true);
                }

                op_specific_data.op_data.end_time     = ext_p_send_op->end_time;
                op_specific_data.op_data.send_data    = output_data.data;
                op_specific_data.op_data.send_size    = output_data.size;
                op_specific_data.op_data.status       = status_e::START_NEW_OP;
                op_specific_data.op_data.send_channel = ext_p_send_op->send_channel;
                p_send_op                             = &op_specific_data.op_data;
                return ext_op_completed(false);
            }

            if (yield_status == yield_e::DONE_UNTIL_NEXT_INPUT)
            {
                ext_p_send_op->end_op();
                ext_p_send_op = nullptr;
                return ext_op_completed(true);
            }

            // error cases (a reset has already occurred at this point):
            status_e error_status{status_e::FILTER_ABORT};
            if (yield_status == yield_e::ABORT_EXCEEDED_STORAGE)
                error_status = status_e::FILTER_ABORT_EXCEEDED_STORAGE;
            else if (yield_status == yield_e::ABORT_DATA_FORMAT_ERROR)
                error_status = status_e::FILTER_ABORT_DATA_FORMAT_ERROR;

            ext_p_send_op->status.set_error_with_additional_info(
                error_status,
                reinterpret_cast<const char *&&>(last_error_location));
            last_error_location = nullptr;
            ext_p_send_op       = nullptr;
            return ext_op_completed(true);
        }

        template <bool is_sending2 = is_sending, typename = typename std::enable_if_t<!is_sending2>>
        constexpr ext_op_completed process_receiving_filter(recv_op *&ext_p_recv_op, recv_op *&p_recv_op)
        {
            // without a filter or external operation, the filter should not run
            if (p_filter == nullptr || ext_p_recv_op == nullptr)
                return ext_op_completed(false);

            // wait for the internal recv to finish, or reset if it is finished
            if (p_recv_op != nullptr)
            {
                if (p_recv_op->is_operating())
                    return ext_op_completed(false);

                // handle any errors
                if (p_recv_op->status != status_e::SUCCESS)
                {
                    ext_p_recv_op->status           = p_recv_op->status;
                    ext_p_recv_op->received_channel = p_recv_op->received_channel;
                    ext_p_recv_op                   = nullptr;
                    p_recv_op                       = nullptr;
                    reset_all();
                    return ext_op_completed(true);
                }

                // pass the receive into the filters input
                input_data.start(p_recv_op->received_data, p_recv_op->received_size);

                // don't request a new internal receive unless the filter says so later on
                p_recv_op = nullptr;
            }

            // consume the ext_p_recv_op
            if (ext_p_recv_op->status == status_e::START_NEW_OP)
            {
                flip_status_from_started_to_in_progress(ext_p_recv_op);

                // ensure that the output of the filter places data directly into the ext_p_recv_op memory
                op_specific_data.user_rx_data = filter::storage_abstract_t{
                    ext_p_recv_op->max_receive_size, ext_p_recv_op->received_data, size_t(0)};

                // in order to propagate the rx storage backwards with a potentially new max_receive_size,
                // the old rx storage needs to be flushed out with a forward pass, since the new receive
                // max size might reach backwards to a different depth if it has a different max size
                p_filter->select_best_storage_backwards_pass(&op_specific_data.user_rx_data);
                p_filter->get_best_input_storage().reset();
            }

            // see if the filter can yield a new output, has an error, or is done until next input
            const yield_e yield_status = yield_next();

            if (yield_status == yield_e::YIELD_ONE_OUTPUT)
            {
                // push receive externally
                ext_p_recv_op->received_size    = output_data.size;
                ext_p_recv_op->received_channel = op_specific_data.op_data.received_channel; // set to the last valid receive channel

                // if the user is receiving smaller amounts of data than the max filter size, than the output same-pointer
                // optimization can't be safely made, and the output data pointer won't be the same as the final data pointer
                if (output_data.data != ext_p_recv_op->received_data)
                {
                    // if the data can't be copied into the ext receive due to it being too small, error out and reset
                    if (output_data.size > ext_p_recv_op->max_receive_size)
                    {
                        const char *error_name = p_filter->get_name();
                        ext_p_recv_op->status.set_error_with_additional_info(
                            status_e::FILTER_OUTPUT_SIZE_OVER_RX_MAX,
                            std::move(error_name));
                        ext_p_recv_op->received_size = 0;
                        ext_p_recv_op                = nullptr;
                        reset_all();
                        return ext_op_completed(true);
                    }

                    // otherwise manually copy the ouput data into the ext receive
                    for (size_t i = 0; i < output_data.size; i++)
                        ext_p_recv_op->received_data[i] = output_data.data[i];
                }

                // mark the output as consumed
                output_data.stop();

                // flush the ext_p_recv_op pointer out of the filter
                p_filter->select_best_storage_forward_pass(nullptr);

                // end the ext_p_recv_op
                ext_p_recv_op->end_op();
                ext_p_recv_op = nullptr;
                return ext_op_completed(true);
            }

            // install a new input request if the filter requests it
            if (yield_status == yield_e::DONE_UNTIL_NEXT_INPUT)
            {
                // if the starting storage is still in use when requesting new input, we
                // can't put new rx data into it without corrupting it, so error out
                auto &starting_rx_storage = p_filter->get_best_input_storage();
                if (starting_rx_storage.size > 0u)
                {
                    const char *error_name = p_filter->get_name();
                    ext_p_recv_op->status.set_error_with_additional_info(
                        status_e::FILTER_STORAGE_NOT_CONSUMED,
                        std::move(error_name));
                    ext_p_recv_op->received_size = 0;
                    ext_p_recv_op                = nullptr;
                    reset_all();
                    return ext_op_completed(true);
                }

                // reset the internal receive op data, except for the received_channel
                const auto last_received_channel = op_specific_data.op_data.received_channel;
                op_specific_data.op_data         = recv_op{
                    common_op{
                        ext_p_recv_op->end_time,
                        status_e::standard_status_e::START_NEW_OP},
                    starting_rx_storage.max_size,
                    starting_rx_storage.data,
                    0u, // received_size
                    last_received_channel};

                // ask for a new input
                p_recv_op = &op_specific_data.op_data;
                return ext_op_completed(false);
            }

            // error cases (a reset has already occurred at this point):
            status_e error_status{status_e::FILTER_ABORT};
            if (yield_status == yield_e::ABORT_EXCEEDED_STORAGE)
                error_status = status_e::FILTER_ABORT_EXCEEDED_STORAGE;
            else if (yield_status == yield_e::ABORT_DATA_FORMAT_ERROR)
                error_status = status_e::FILTER_ABORT_DATA_FORMAT_ERROR;

            ext_p_recv_op->status.set_error_with_additional_info(
                error_status,
                reinterpret_cast<const char *&&>(last_error_location));
            last_error_location = nullptr;
            ext_p_recv_op       = nullptr;
            p_recv_op           = nullptr;
            return ext_op_completed(true);
        }

    private:
        const char *last_error_location            = nullptr;
        filter::data_t input_data                  = {};
        filter::data_t output_data                 = {};
        filter::result_e result                    = filter::result_e::CONTINUE;
        std::shared_ptr<filter::abstract> p_filter = nullptr;

        static constexpr void flip_status_from_started_to_in_progress(common_op *op_ptr)
        {
            if (op_ptr != nullptr && op_ptr->status == status_e::START_NEW_OP)
                op_ptr->status = status_e::OP_IN_PROGRESS;
        }

        struct receiving_specific_t
        {
            recv_op op_data                         = {};
            filter::storage_abstract_t user_rx_data = {};
        };
        struct sending_specific_t
        {
            send_op op_data = {};
        };
        using op_specific_type            = typename std::conditional<is_sending, sending_specific_t, receiving_specific_t>::type;
        op_specific_type op_specific_data = {};

        constexpr void reset_all()
        {
            last_error_location = nullptr;
            input_data.stop();
            output_data.stop();
            result = filter::result_e::CONTINUE;
            if (p_filter != nullptr)
            {
                p_filter->select_best_storage_forward_pass(nullptr);
                p_filter->reset_all();
            }
            op_specific_data = op_specific_type{}; // note: .reset() isn't used here because it would leak rx memory pointers
        }

        enum class yield_e
        {
            DONE_UNTIL_NEXT_INPUT,
            YIELD_ONE_OUTPUT,
            ABORT,
            ABORT_EXCEEDED_STORAGE,
            ABORT_DATA_FORMAT_ERROR
        };

        constexpr yield_e yield_next()
        {
            if (result == filter::result_e::CONTINUE && !input_data)
                return yield_e::DONE_UNTIL_NEXT_INPUT;
            do
            {
                result = p_filter->process(input_data, output_data);
                if (result.is_aborted())
                {
                    const char *loc = result.location_of_abort();
                    reset_all();
                    last_error_location = loc;
                    if (result == filter::result_e::ABORT_EXCEEDED_STORAGE)
                        return yield_e::ABORT_EXCEEDED_STORAGE;
                    if (result == filter::result_e::ABORT_DATA_FORMAT_ERROR)
                        return yield_e::ABORT_DATA_FORMAT_ERROR;
                    return yield_e::ABORT;
                }
                if (output_data)
                    return yield_e::YIELD_ONE_OUTPUT;
            } while (result == filter::result_e::FORCE_TO_KEEP_PROCESSING);
            return yield_e::DONE_UNTIL_NEXT_INPUT;
        }
    };

} // namespace interface

#endif // CPPTXRX_FILTER_PROCESSOR_H_