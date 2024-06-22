![cpptxrx_icon](images/cpptxrx_icon.png)

# Cpp Tx/Rx Interface Wrapper

Helps you take existing low-level communication libraries and turn them into high level easy-to-use C++ sending and receiving interfaces with effortless thread-safety and polymorphism.

![license](https://img.shields.io/badge/license-MIT-informational) ![version](https://img.shields.io/badge/version-1.0-blue)

## Features

* C++17 or greater.
* Header only, with no dependencies.
* Choose from two main interfaces:
  * `interface::thread_safe` - Inherit from this thread-safe base class, and call any method from any thread without worry.
  * `interface::raw` - Non-thread-safe without the extra thread overhead, but provides access to the same common API.
* Polymorphic:
  * Want to use different data transport mechanisms for different situations or platforms, such as UDP for a simulator, and UART for your target platform? Or just increase ease-of-porting? Just write your code to expect a `interface::abstract` class and use polymorphism.
  * [examples/using_polymorphism.cpp](examples/using_polymorphism.cpp)
* Pre-made default implementation(s) for:
  * Threadsafe UDP: [include/default_udp.h](include/default_udp.h)
  * Raw UDP: [include/default_udp_raw.h](include/default_udp_raw.h)
  * ... More to come.

## Motivation

Creating thread-safe C++ class wrappers for messaging interfaces is often non-trivial, tedious, and bug prone, often needing to be done repeatedly for each new interface type your code needs to support. Added to this difficulty, many communication libraries and hw-drivers are either not thread safe at all such as **posix sockets**, **zeromq**, **UART**, **SPI**, **1553**, etc., or they are only partially thread safe, such as **Boost ASIO's** sockets. If the library you want to use doesn't provide a thread safe API, then you might just be out of luck without implementing your own wrapper. Luckily, many libraries provide good single thread APIs that allow I/O multiplexing similar to the **select** function for posix sockets, so this library takes advantage of that to create a standardized wrapper-template for interfaces to make them thread safe. Following the template can let you avoid the problem of thread safety entirely. Just define a few **non-thread-safe** interaction functions, and the library will use them to create a thread safe interface. While we're at it, the library also tries to make each new interface you define look as close as possible to all the others. Additionally a non-thread-safe version of the wrapper is available, if you don't need the extra thread safety overhead, but you still want to keep a consistent interface API. When used with multiple platforms and interfaces, swapping between different data transport mechanisms becomes trivial.

## Examples

For detailed and runnable examples, see the [examples](examples) folder.

### How to create a new interface in FIVE steps

```cpp
// Step 1) include either cpptxrx_threadsafe.h (to make a threadsafe interface) or cpptxrx_raw.h (to create a non threadsafe interface)
#include "cpptxrx_threadsafe.h"

namespace udp
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
        IMPORT_CPPTXRX_CTOR_AND_DTOR(socket);

    private:
        // Step 5) provide override definitions of the following methods to implement your interface:

        // [[OPTIONAL]] Define this instead of a constructor, if needed, so that you can call virtual methods in this constructor.
        void construct() override;

        // [[OPTIONAL]] Define this instead of a destructor, if needed.
        void destruct() override;

        // [[REQUIRED]] Meant to handle the close operation, held in the "transactions.p_close_op" variable, which is never nullptr in the method.
        void process_close() override;

        // [[REQUIRED]] Meant to handle the open operation, held in the "transactions.p_open_op" variable, which is never nullptr in the method.
        void process_open() override;

        // [[REQUIRED]] Meant to handle a send operation, a receive operation, or both simultaneously.
        // The "transactions.p_send_op", and "transactions.p_receive_op" pointers are not nullptr when their operation is requested.
        void process_send_receive() override;

        // [[only REQUIRED for interface::threadsafe]] Used to wake up a process_open/close/send_receive call that is blocking when another operation is requested.
        // WARNING!: wake_process is the only "overridden" method that can be called from other threads
        // WARNING!: The wake signal must be sticky (like a eventfd object), since there's no guarantee that wake_process
        //           will be called precisely when your process_ method is performing a block or reading the wake signal.
        void wake_process() override;
    };
} // namespace udp
```

## Example of how to use an interface

```cpp
// create instances of your interfaces:
udp::socket client(udp::socket::opts()
                       .role(udp::role_e::CLIENT)
                       .port(1230)
                       .ipv4_address("127.0.0.1"));

udp::socket server(udp::socket::opts()
                       .role(udp::role_e::SERVER)
                       .port(1230)
                       .ipv4_address("127.0.0.1"));

// you can treat status as an enum, like "client.open_status() == interface::status_e::SUCCESS", or look at the c_str version:
printf("server open status: %s\n", server.open_status().c_str());
printf("client open status: %s\n", client.open_status().c_str());

// then send and receive across multiple threads without worry
interface::raii_thread thread1(
    [&]()
    {
        uint8_t tx_data[] = "hello 1!";
        thread_safe_printf("client sending: \"%s\"\n", tx_data);
        client.send(tx_data, sizeof(tx_data));
    });

interface::raii_thread thread2(
    [&]()
    {
        uint8_t tx_data[] = "hello 2!";
        thread_safe_printf("client sending: \"%s\"\n", tx_data);
        client.send(tx_data, sizeof(tx_data));

        uint8_t rx_data[100] = {};
        auto rx_result_info  = client.receive(rx_data, sizeof(rx_data));
        thread_safe_printf("client received: \"%s\" (size=%zu bytes, status=%s)\n", rx_data, rx_result_info.size, rx_result_info.status.c_str());
    });

interface::raii_thread thread3(
    [&]()
    {
        uint8_t rx_data[100] = {};
        auto rx_result_info  = server.receive(rx_data, sizeof(rx_data));
        thread_safe_printf("server received: \"%s\" (size=%zu bytes, status=%s)\n", rx_data, rx_result_info.size, rx_result_info.status.c_str());

        uint8_t tx_data[] = "hello 3!";
        thread_safe_printf("server sending: \"%s\"\n", tx_data);
        server.send(tx_data, sizeof(tx_data));
    });

interface::raii_thread thread4(
    [&]()
    {
        uint8_t rx_data[100] = {};
        auto rx_result_info  = server.receive(rx_data, sizeof(rx_data));
        thread_safe_printf("server received: \"%s\" (size=%zu bytes, status=%s)\n", rx_data, rx_result_info.size, rx_result_info.status.c_str());
    });
```

## FAQ

### 1. I want my interface to have additional public methods, besides open/close/send/receive, does that mean I need to modify the CppTxRx code?

You can extend your derived interface classes by adding any interface-specific methods you want. However it's not recommended you force application specific API changes to flow up into the abstract/generic class. As an example, if you added a `start_1553_bus_monitor()` method into the `interface::abstract` class, then a TCP interface inheriting from that abstract class would suddenly gain an irrelevant and potentially confusing method.

### 2. I want my interface to have additional public methods, besides open/close/send/receive, will they also be automatically thread safe?

No. CppTxRx guarantees that the existing API will be threadsafe, and that the following five overridden methods are guaranteed to be called from a single thread:

```cpp
void construct() override;
void destruct() override;
void process_close() override;
void process_open() override;
void process_send_receive() override;
```

So, using that guarantee to help you reason about thread safety, you should be able to write extensions that properly account for the threaded environment.

## A note about why "inheritance" was chosen over "composition"

A choice between inheritance and composition was made when choosing a wrapping method, since instead of inheriting an interface base:

```cpp
class new_interface : public interface::thread_safe<opts>
{
public:
    IMPORT_CPPTXRX_CTOR_AND_DTOR(new_interface);
private:
    void construct() override;
    void destruct() override;
    void process_close() override;
    void process_open() override;
    void process_send_receive() override;
    void wake_process() override;
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
    IMPORT_CPPTXRX_CTOR_AND_DTOR(new_interface);
private:
    void construct() override;
    void destruct() override;
    // user_extensions...
};
```

Which means that in order to get the same functionality and extendability as the original inheritance version, using composition, the interface needs to be split into two class definitions instead of one. Where sharing state can be extremely complex, and even circularly dependent. Lifetime management becomes a bit harder in the base class, and we still have to deal with avoiding virtual constructor calls by introducing custom construct/destruct overrides. So no work is actually saved using composition
