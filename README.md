![cpptxrx_icon](images/cpptxrx_icon.png)

# Cpp Tx/Rx Interface Wrapper

A framework for turning existing low-level communication libraries into high-level/easy-to-use C++ interface classes with effortless thread-safety, reusable extensibility, and polymorphism.

![license](https://img.shields.io/badge/license-MIT-informational) ![version](https://img.shields.io/badge/version-1.1-blue)

## Features

* **C++17 or greater.**

* **Header only, with no dependencies beyond the C++ stdlib.**

* **Choose from two main interfaces:**
  * `interface::thread_safe` - Inherit from this thread-safe base class, and call any method from any thread without worry.
  * `interface::raw` - Non-thread-safe without the extra thread overhead, but provides access to the same common API and library of compatible features.

* **Extensibility:**
  * `interface::filter` - Plug-and-play data manipulation components, that can apply generic procedures to any send or receive operation, on any interface. They can even be chained together, and will automatically optimize away extra unnecessary data copies. See: [examples/04_chaining_filters.cpp](examples/04_chaining_filters.cpp)
  * `interface::receive_callback::create` - Use callback functions that process data as it arrives, instead of calling receive manually.
  [examples/03_receive_callbacks.cpp](examples/03_receive_callbacks.cpp)
  * `interface::abstract` - Polymorphic:
    * Want to use different data transport mechanisms for different situations or platforms, such as UDP for a simulator, and UART for your target platform? Or just increase ease-of-porting? Just write your code to expect a `interface::abstract` class and use polymorphism. See: [examples/05_using_runtime_polymorphism.cpp](examples/05_using_runtime_polymorphism.cpp) and [examples/06_using_comptime_polymorphism.cpp](examples/06_using_comptime_polymorphism.cpp)
    * Filters and callbacks also have polymorphic options:
      * `interface::filter::abstract`
      * `interface::receive_callback::abstract`

* **Pre-made interface implementation(s) for:**
  * Raw UDP: [include/default_udp_raw.h](include/default_udp_raw.h)
  * Threadsafe UDP: [include/default_udp.h](include/default_udp.h)
  * Threadsafe TCP: [include/default_tcp.h](include/default_tcp.h)
  * ... More to come.

* **Pre-made send/receive filters in [include/cpptxrx_filters.h](include/cpptxrx_filters.h) for:**
  * [SLIP](https://en.wikipedia.org/wiki/Serial_Line_Internet_Protocol): `interface::filters::slip::encode` and `interface::filters::slip::decode`
  * [Delimiters](https://en.wikipedia.org/wiki/Delimiter): `interface::filters::delimit`
  * Size-Based Aggregation and Splitting: `interface::filters::enforce_fixed_size` and `interface::filters::split_if_larger`
  * Repeating: `interface::filters::repeat`
  * Appending: `interface::filters::append`
  * ... and more

* **Performance and Safety:**
  * Threading:
    * Takes a very simple approach to thread safety: Isolates all your low-level communication library's API calls to a single dedicated thread (construction, opening, sending, receiving, closing, and destruction). Then gives you efficient/safe ways of interfacing with that thread using the cpptxrx API, which behind the scenes uses a single bit-masked integer for speedy communication.
  * Memory:
    * Avoids all dynamic memory allocation, such as `std::vector`/`std::function`/etc., with the possible exception of any dynamic allocations your platform's stdlib implementation might need for OS primitives, such as the single `std::thread` instance in the `interface::thread_safe` class. Note that you can still allow dynamically allocated object usage by explicitly using `interface::allow_heap(object)` for filter and callback objects.

## Motivation

In embedded software (space-software and robotics specifically), to make communication infrastructure portable it is often desirable to support and switch between different data transport mechanisms (UART/SPI/UDP/TCP/1553/SpaceWire/CAN/etc.), sometimes even dynamically at runtime. Each type of communication library will have its own way of handling thread safety, or simply neglect it, leading to wrapper classes that reinvent the same types of action dispatching and thread safety protection mechanisms for each new interface, highly coupling thread safety management with that interface's particular needs unnecessarily. This tool lets you accomplish interface wrapping a bit more easily, by handling the bulk of the thread safety considerations for you. The standardized interface also encourages you to make interfaces look and behave similarly, so that swapping between them become a trivial endeavor, rather than a case-by-case architecture refactoring effort. This standardization also allows for the definition of reusable plug-and-play data processing blocks (`interface::filters`), that can be written just once and later chained together in infinite combinations, work automatically with any wrapped interface.

## Examples

For detailed and runnable examples, see the [examples](examples) folder.

### Receive Callbacks: Functions to get called when new data is receive

```cpp
using namespace ctr;

auto receive_function = receive_callback::create(
    [](recv_op &received_data)
    {
        // this function will be called automatically when receiving data
    });
```

### Filters: Data processing modules that can be attached to an interface's inputs or outputs

```cpp
// there are pre-made filters in the "filters" namespace:
auto slip_encoder = filters::slip::encode();
auto slip_decoder = filters::slip::decode();

// make your own filters, using filter::base_class, or filter::create() like this:
auto custom_decoder = filter::create(
    [](storage_abstract_t &working_memory, data_t &input, data_t &output)
    {
        // copy the input into the filter's working memory (copy_to_lazily will automatically skip doing a real memory copy if it's able)
        input.copy_to_lazily(working_memory);

        // do some extra data manipulation, such as adding some extra data
        working_memory.append(0xAB);

        // start outputting the working memory once it's ready
        output.start_and_consume(working_memory);

        // indicate that we're done with consuming the input data
        input.stop();

        // continue until another iteration
        return filter::result_e::CONTINUE;
    });
```

### Chaining: Combining data processing filters into pipelines

```cpp
// For example, to implement the data pipeline:
//    send or received data -> custom_decode -> SLIP encode -> SLIP decode
// Just use the ".then()" method:
auto complex_data_processing = custom_decoder.then(slip_encoder).then(slip_decoder);
```

A note on performance: Filters, even ones created by chaining are fast!! Behind the scenes data copies in the filters are automatically minimized, and even skipped when possible, so that as many manipulations happen in place as possible. Think of it less like combining several algorithms in series where each algorithm is still isolated form the others, and more like merging several algorithms into one integrated/optimized algorithm, without you having to manually do that integration and optimization.

### Constructing interfaces

```cpp
// this constructs a TCP server that you can open with .open() later on:
tcp::socket<tcp::role_e::SERVER, max_number_of_clients> server1;

// this constructs a TCP server and then calls .open() for you since open arguments were provided:
tcp::socket<tcp::role_e::SERVER, max_number_of_clients> server2(
    // opts() are connection-specific options:
    tcp::opts()
        .port(2240)
        .ipv4_address("127.0.0.1"),

     // common_opts() are options that can plug-and-play into ANY interface
     //                    you do not need to re-implement them :)
    common_opts()
        .open_timeout(std::chrono::seconds(3))
        .receive_callback(&receive_function)
        .receive_filter(&slip_decoder)
        .send_filter(&slip_encoder)
        .auto_reopen_after(std::chrono::seconds(3))
    );
```

### Using interfaces

```cpp
// send and receive across any number of threads without worry
raii_thread thread1([&]() {
    client.send("hello 1!");
});
raii_thread thread2([&]() {
    client.send("hello 2!");

    uint8_t rx_data[100] = {};
    auto rx_result  = client.receive(rx_data);
    thread_safe_printf("client received: '%s' (size=%zu bytes, status=%s)\n",
        rx_data, rx_result.size, rx_result.status.c_str());
});
raii_thread thread3([&]() {
    uint8_t rx_data[100] = {};
    auto rx_result  = server.receive(rx_data);
    thread_safe_printf("server received: '%s' (size=%zu bytes, status=%s)\n",
        rx_data, rx_result.size, rx_result.status.c_str());

    server.send("hello 3!");
});
raii_thread thread4([&]() {
    uint8_t rx_data[100] = {};
    auto rx_result  = server.receive(rx_data);
    thread_safe_printf("server received: '%s' (size=%zu bytes, status=%s)\n",
        rx_data, rx_result.size, rx_result.status.c_str());
});
```

### How to create a new interface in FIVE steps

```cpp
// Step 1) include either cpptxrx_threadsafe.h (to make a threadsafe interface) or cpptxrx_raw.h (to create a non threadsafe interface)
#include "cpptxrx_threadsafe.h"

namespace your_interface_name
{
    // Step 2) define what open() options your new interface will need by defining an "opts" type
    struct opts
    {
        int m_port= 0;
        uin32_t m_address = 0;
        CPPTXRX_OPTS_SETTER(port); // the CPPTXRX_OPTS_SETTER macro is just a quick way of making method-chaining setters
        CPPTXRX_OPTS_SETTER(address);
    };

    // Step 3) inherit from either "interface::thread_safe<opts>" to create a thread safe interface
    // or "interface::raw<opts>" for a non-threadsafe interface
    class socket : public interface::thread_safe<opts>
    {
    public:
        // Step 4) some of the necessary constructor/destructor functions must be imported
        CPPTXRX_IMPORT_CTOR_AND_DTOR(socket);

    private:
        // Step 5) provide override ("final" overrides are recommended) definitions of the following methods to implement your interface:

        // [[OPTIONAL]] Define this instead of a constructor, if needed, so that you can call virtual methods in this constructor.
        void construct() final;

        // [[OPTIONAL]] Define this instead of a destructor, if needed.
        void destruct() final;

        // [[REQUIRED]] Meant to handle the close operation, held in the "transactions.p_close_op" variable, which is never nullptr in the method.
        void process_close() final;

        // [[REQUIRED]] Meant to handle the open operation, held in the "transactions.p_open_op" variable, which is never nullptr in the method.
        void process_open() final;

        // [[REQUIRED]] Meant to handle a send operation, a receive operation, or both simultaneously.
        // The "transactions.p_send_op", and "transactions.p_receive_op" pointers are not nullptr when their operation is requested.
        // NOTE: If you have a connection that needs continuous maintenance (like waiting to accept new TCP clients), then you can
        // set "transactions.idle_in_send_recv = true" and process_send_receive will be run even when no operations have been requested.
        void process_send_receive() final;

        // [[only REQUIRED for interface::threadsafe]] Used to wake up a process_open/close/send_receive call that is blocking when another operation is requested.
        // WARNING!: wake_process is the only "overridden" method that can be called from other threads
        // WARNING!: The wake signal must be sticky (like a eventfd object), since there's no guarantee that wake_process
        //           will be called precisely when your process_ method is performing a block or reading the wake signal.
        void wake_process() final;
    };
} // namespace your_interface_name
```

## FAQ

### 1. I want my interface to have additional public methods, besides open/close/send/receive, does that mean I need to modify the CppTxRx code?

You can extend your derived interface classes by adding any interface-specific methods you want. However it's not recommended you force application specific API changes to flow up into the abstract/generic class. As an example, if you added a `start_FPGA_UART_processor()` method into the `interface::abstract` class, then a TCP interface inheriting from that abstract class would suddenly gain an irrelevant and potentially confusing method. Abstraction is all about knowing the value of exposing specific details, and then assessing whether the value in exposing that specific detail directly outweighs the value gained from using a generalization instead.

### 2. I want my interface to have additional public methods, besides open/close/send/receive, will they also be automatically thread safe?

No. CppTxRx guarantees that the existing API will be threadsafe, and that the following five overridden methods are guaranteed to be called from a single thread:

```cpp
void construct() final;
void destruct() final;
void process_close() final;
void process_open() final;
void process_send_receive() final;
```

So, using that guarantee to help you reason about thread safety, you should be able to write extensions that properly account for the threaded environment.

## A note about why "inheritance" was chosen over "composition"

A choice between inheritance and composition was made when choosing a wrapping method, since instead of inheriting an interface base:

```cpp
class new_interface : public interface::thread_safe<opts>
{
public:
    CPPTXRX_IMPORT_CTOR_AND_DTOR(new_interface);
private:
    void construct() final;
    void destruct() final;
    void process_close() final;
    void process_open() final;
    void process_send_receive() final;
    void wake_process() final;
    // user_extensions...
};
```

Composition could be used, specifically, by passing an interface definition into a template argument. Which at first glance does look a bit simpler:

```cpp
struct interface_definition
{
    interface_definition();
    ~interface_definition();
    void process_close(interface::transactions_args<opts> &conn);
    void process_open(interface::transactions_args<opts> &conn);
    void process_send_receive(interface::transactions_args<opts> &conn);
    void wake_process();
};
using new_interface = interface::create_thread_safe<interface_definition>;
```

However, this second method of "composition" introduces additional complexities. Since, when the end-user attempts to extend the interface with custom features, they're likely going to try and use inheritance anyway. With inheritance, calling constructors and destructors in a thread safe way becomes difficult without reintroducing the concepts of override construct/destruct methods. Leading to the user needing to write something like this in order to add any extensions:

```cpp
struct interface_definition
{
    interface_definition();
    ~interface_definition();
    void process_close(interface::transactions_args<opts> &conn);
    void process_open(interface::transactions_args<opts> &conn);
    void process_send_receive(interface::transactions_args<opts> &conn);
    void wake_process();
};
class new_interface : public interface::create_thread_safe<interface_definition>;
{
public:
    CPPTXRX_IMPORT_CTOR_AND_DTOR(new_interface);
private:
    void construct() final;
    void destruct() final;
    // user_extensions...
};
```

Which means that in order to get the same functionality and extendability as the original inheritance version, using composition, the interface needs to be split into two class definitions instead of one. Where sharing state can be extremely complex, and even circularly dependent. Lifetime management becomes a bit harder in the base class, and we still have to deal with avoiding virtual constructor calls by introducing custom construct/destruct overrides. So no work is actually saved using composition, and inheritance was ultimately chosen as the preferred baseline.
