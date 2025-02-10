/// @brief This example demonstrates how filters can be used and even chained together
//
// Data filters are functional objects that can process arbitrary bytes of data you send or
// receive. They can hold onto their own state and storage, and they can be used on any
// interface, for either the sending or receiving directions. For example, if you're sending comma
// separated values, you might want a filter which breaks apart the stream of data, so that
// each value is in it's own receive call.
//      That can be accomplished simply by using the filter:   delimit(",")
//
// Filters are is very similar to traditional data pipelines in that they can be chained together,
// you can think of them like a series of function calls, like this:
//     output = filter1(filter2(filter3(input)));      // using function call syntax
//     output = input | filter3 | filter2 | filter1;   // using pipe operator syntax
//
// However, CppTxRx filters are better in two ways!
//   1) Flexibility:
//       The number of outputs doesn't have to equal the number of inputs (think of it like the
//       python yield statements). So segmenting and combining data is trivially achievable.
//   2) Speed:
//       When processing the data, the CppTxRx backend will automatically figure out the optimal way
//       of reducing the number of memory copies that need to be done throughout your pipeline of filters.
//       For example, instead of taking in a copy of the input data in a receive operation,
//       and then mutating it in a filter, and then copying it into the ouput receive buffer (two extra
//       copies). If the filter doesn't indicate it needs a new copy, all the operations can be
//       done in-place on the final receive buffer, without ANY extra copies, or any extra code!
//
// You can define new data filters via three options:
//   1) using a method (by inheriting from filter::base_class)
//   2) using a callable object - like a lambda function (by using filter::create(...))
//   3) through the chaining of existing filters into a new filter (by doing "filter1.then(filter2)")
//
// For more examples, read through the various pre-defined filters to get a sense for how they're
// implemented. Located in the "include/cpptxrx_filters.h" file.

// here we'll define a simple pretend_socket class, and then a few example filters
#include "../include/cpptxrx_threadsafe.h"
#include "utils/printing.h"

// using the shorter ctr namespace for succinctness
using namespace ctr;

/// creating a pretend_socket interface that isn't hooked up to anything,
/// it just prints sends, and receives some simple hard-coded data
struct demo_interface : interface::thread_safe<interface::no_opts>
{
    CPPTXRX_CREATE_EMPTY_DEMO_METHODS(demo_interface);
    void process_send(interface::send_op &op)
    {
        thread_printf("\t--[X]-> post-filter sending: \"%s\"\n", op.send_data);
        op.end_op();
    }
    void process_recv(interface::recv_op &op)
    {
        uint8_t rx_data[] = "hello.HELLO.";
        op.copy_data_and_end_op(rx_data, sizeof(rx_data) - 1u);
        thread_printf("\t  [ ]<- pre-filter receive: \"%s\"\n", rx_data);
    }
};

/// how to create a new filter type using a "method" as the filter definition
template <size_t max_size = default_max_packet_size>
struct add_comma_after_each_byte final : filter::base_class<add_comma_after_each_byte<max_size>, max_size>
{
    inline result_e operator()(data_t &input, data_t &output) final
    {
        auto &working_memory = this->get_best_output_storage();

        // loop through each input byte, and append a comma after each byte
        for (const uint8_t &element : input)
            working_memory << element << ',';

        input.stop();                             // indicate we're done with the input data
        output.start_and_consume(working_memory); // and pass the storage array into the output data, consuming the storage
        return result_e::CONTINUE;                // yield until next input
    };
    inline void reset() final {}
    ~add_comma_after_each_byte() final = default;
};

/// how to create a new filter object using a "lambda" as the filter definition:
auto add_counter_prefix = filter::create(
    [counter = '0'](storage_abstract_t &working_memory, data_t &input, data_t &output) mutable
    {
        working_memory << "prefix_" << counter++ << ' ' << consume(input);
        output.start_and_consume(working_memory);
        return result_e::CONTINUE;
    });

/// how to chain filters together to create new ones:
auto summon_beetlejuice = append("juice").then(repeat(3));

/// another more complex example of filter chaining
auto split_append_and_exclaim = delimit(".")
                                    .then(append("-world"))
                                    .then(append("!"))
                                    .then(add_counter_prefix);

// note that chained filters can break apart one input into multiple outputs
// or even combine multiple inputs into fewer outputs
// for example, try adding a .then(delimit("-")) to the end of split_append_and_exclaim
// to see data getting combined together up to the delimiter.

int main()
{
    // attach filters to either the receive or send pipelines of an interface:
    demo_interface pretend_socket(
        common_opts()
            .receive_filter(&split_append_and_exclaim)
            .send_filter(&summon_beetlejuice));

    // Note that in the above pretend_socket, the filters are just passed in as pointers,
    // so the filter object's lifetime is managed externally to the interface object,
    // creating the potential for a pointer leak if you're not careful.
    //
    // If instead you want the interface to manage the filter's lifetime/memory for you,
    // simply use "allow_heap(filter_type())" in order dynamically allocate a dedicated
    // filter object that doesn't need to point to anything external - with the trade-off
    // that you'll be using dynamic memory allocation.
    //
    // Like this:
    //     .receive_filter(allow_heap(split_append_and_exclaim)))
    //     .send_filter(allow_heap(summon_beetlejuice))

    // now that the filter is attached, we can print out the receives to see it in action!
    thread_printf("\nreceive filter examples:\n");
    for (size_t i = 0; i < 4; i++)
    {
        uint8_t received_data[default_max_packet_size] = {};
        pretend_socket.receive(received_data);
        thread_printf("\t<-[X]-- post-filter receive: \"%s\"\n", received_data);
    }
    // Which will print out:
    //
    // receive filter examples:
    //       [ ]<- pre-filter receive: "hello.HELLO."
    //     <-[X]-- post-filter receive: "prefix_0 hello-world!"
    //     <-[X]-- post-filter receive: "prefix_1 HELLO-world!"
    //       [ ]<- pre-filter receive: "hello.HELLO."
    //     <-[X]-- post-filter receive: "prefix_2 hello-world!"
    //     <-[X]-- post-filter receive: "prefix_3 HELLO-world!"

    // and, lets also see the send filters in action!
    thread_printf("\nsend filter examples:\n");
    uint8_t sent_data[] = "Beetle";
    thread_printf("\t->[ ]   pre-filter send: \"%s\"\n", sent_data);
    pretend_socket.send(sent_data, sizeof(sent_data) - 1u);

    // Which will print out:
    //
    // send filter examples:
    //     ->[ ]   pre-filter send: "Beetle"
    //     --[X]-> post-filter sending: "Beetlejuice"
    //     --[X]-> post-filter sending: "Beetlejuice"
    //     --[X]-> post-filter sending: "Beetlejuice"
    return 0;
}