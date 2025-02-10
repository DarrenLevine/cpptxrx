/// @file cpptxrx_factory.h
/// @author Darren V Levine (DarrenVLevine@gmail.com)
/// @brief This file contains the class definition(s) for both the threadsafe and raw interface base classes,
/// as a single class CPPTXRX_CLASS_NAME definiton. These classes act as base classes for creating new wrapped
/// cpptxrx interface classes, either threadsafe or non-threadsafe (raw). Because both classes are so similar,
/// with a few small differences here and there, the threadsafe specific code is simply separated by a
/// CPPTXRX_THREADSAFE macro #ifdef. So that two separate classes can be generated using this one file, and
/// maintenance is easier.
///
/// @copyright (c) 2024 Darren V Levine. This code is licensed under MIT license (see LICENSE file for details).
///
#ifndef CPPTXRX_FACTORY_H_
#define CPPTXRX_FACTORY_H_

#include "cpptxrx_abstract.h"
#include "cpptxrx_filter_processor.h"
#include "cpptxrx_macros.h"
#include "cpptxrx_op_backend.h"
#include <cassert>

#ifdef CPPTXRX_CLASS_NAME
#undef CPPTXRX_CLASS_NAME
#endif

// this switches between the threadsafe_factory and raw_factory class names:
#if CPPTXRX_THREADSAFE
#define CPPTXRX_CLASS_NAME threadsafe_factory
#include "cpptxrx_raii_thread.h"
#include <condition_variable>
#include <mutex>
#else
#define CPPTXRX_CLASS_NAME raw_factory
#endif

namespace interface
{
    /// @brief a generator for an inheritable base class for creating both thread safe and non thread safe CppTxRx interfaces
    ///
    /// @tparam   open_opts_type: options type to use when calling "open"
    /// @tparam   timeouts<...> (optional): default recv, send, open, and close timeouts in ns, only used when no timeout
    ///               is specfied on the operation.
    template <typename open_opts_type, typename default_timeouts_type = timeouts<>>
    class CPPTXRX_CLASS_NAME : protected transactions_args<open_opts_type>, public abstract
    {
    public:
        /// @brief options type to use when calling "open"
        using opts             = open_opts_type;
        using default_timeouts = default_timeouts_type;

        /// @brief Construct a new cpptxrx interfaces object, and if running in thread safe mode, spools
        /// up a management thread where all interface interactions will occur, and that all other methods
        /// will simply dispatch requests to.
        CPPTXRX_CLASS_NAME()
        {
#if CPPTXRX_THREADSAFE
            std::unique_lock<std::mutex> lk0(m);
            std::lock_guard<std::mutex> open_opts_lk0(m_open_opts_mutex);

            // must construct the thread after the full memory initialization, since a race condition can occur if
            // the thread is created in the initializer list, due to the virtual table not being initialized yet
            thread_handle = std::thread(
                [this]()
                {
                    // need to wait until the child class request an operation, otherwise we can't be
                    // sure that the child class's virtual methods are safe to be called
                    {
                        std::unique_lock<std::mutex> lk(m);
                        cv.wait(lk,
                                [this]()
                                {
                                    return active_ops.is_any(backend::op_bitmasks::ANY_REQUEST);
                                });

                        // now it's safe to call virtual methods, so construct the child class
                        std::lock_guard<std::mutex> open_opts_lk(m_open_opts_mutex); // just in case the open opts are modified in construct
                        construct();
                    }

                    // now it's safe to allow transactions after construction, so mark as constructed
                    mark_as_constructed();

                    // transact until a destroy operation is requested
                    while (single_operation())
                    {
                    }

                    // destruct
                    {
                        std::lock_guard<std::mutex> lk(m);
                        std::lock_guard<std::mutex> open_opts_lk(m_open_opts_mutex); // just in case the open opts are modified in destruct
                        destruct();

                        // notify destruct is complete
                        active_ops.complete_request(backend::op_category_e::DESTROY);
                    }
                    cv.notify_all();
                    // the derived class will be destructed after this notify. So do not add any code here!
                });
#else
            // NOTE: there cannot be a virtual construct() called here, since the child class won't exist yet
            // so instead the child class is required to use CPPTXRX_IMPORT_CTOR_AND_DTOR to call its own construct() method
#endif
        }

        /// @brief Destroy the abstract layer of the cpptxrx interface object.
        /// NOTE: The derived class' destructor MUST call destroy (which will be done if
        /// the CPPTXRX_IMPORT_CTOR_AND_DTOR macro is used), since the constructor and
        /// destructor of a parent class can't call the virtual methods of a child
        /// class safely.
        virtual ~CPPTXRX_CLASS_NAME() = default;

        CPPTXRX_CLASS_NAME operator=(const CPPTXRX_CLASS_NAME &) = delete;
        CPPTXRX_CLASS_NAME(const CPPTXRX_CLASS_NAME &)           = delete;

        void destroy() final
        {

#if CPPTXRX_THREADSAFE
            {
                std::unique_lock<std::mutex> lk(m);
                if (active_ops.is_any(backend::op_bitmasks::ANY_DESTROY))
                {
                    // if another thread requested a destruction, just wait for it to finish
                    cv.wait(lk, [this]()
                            { return active_ops.is_complete(backend::op_category_e::DESTROY); });
                    return;
                }

                active_ops.start_request(backend::op_category_e::DESTROY);
                safe_wake_process(lk);
            }

            cv.notify_all();

            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [this]()
                        { return active_ops.is_complete(backend::op_category_e::DESTROY); });
            }
#else
            if (active_ops.is_any(backend::op_bitmasks::ANY_DESTROY))
                return; // don't re-destroy
            active_ops.start_request(backend::op_category_e::DESTROY);
            single_operation();
            destruct();
#endif
        }

        // NOTE: Detailed documentation is in cpptxrx_abstract.h for all "override" methods (the methods that don't use "opts")
        //       Only the methods that are NOT overrides are documented here, so if you're not using intellisense and notice
        //       missing documentation, check for it in cpptxrx_abstract.h.

        using abstract::open;
        using abstract::reopen;

        // Various open methods, letting you pick what happens if open is called on an already open connection, and pick whether to reuse settings
        status_e reopen() final
        {
            return open_and_reopen_pattern<open_behaviour_e::CLOSE_FIRST_IF_ALREADY_OPEN>(nullptr, nullptr);
        }
        status_e open() final
        {
            return open_and_reopen_pattern<open_behaviour_e::FAIL_TO_OPEN_IF_ALREADY_OPEN>(nullptr, nullptr);
        }
        status_e reopen(common_opts open_config) final
        {
            return open_and_reopen_pattern<open_behaviour_e::CLOSE_FIRST_IF_ALREADY_OPEN>(&open_config, nullptr);
        }
        status_e open(common_opts open_config) final
        {
            return open_and_reopen_pattern<open_behaviour_e::FAIL_TO_OPEN_IF_ALREADY_OPEN>(&open_config, nullptr);
        }

        /// @brief reopen using new settings (reopen will close first if already open - use open if not desired), with
        /// additional abstract open configurations
        ///
        /// @param    settings: new connection specific open settings to use
        /// @param    open_config: abstract open configuration settings
        /// @return   status_e: the final status of the operation
        status_e reopen(opts settings, common_opts open_config)
        {
            return open_and_reopen_pattern<open_behaviour_e::CLOSE_FIRST_IF_ALREADY_OPEN>(&open_config, &settings);
        }

        /// @brief reopen using new settings (reopen will close first if already open - use open if not desired), with
        /// additional abstract open configurations
        ///
        /// @param    open_config: abstract open configuration settings
        /// @param    settings: new connection specific open settings to use
        /// @return   status_e: the final status of the operation
        status_e reopen(common_opts open_config, opts settings)
        {
            return open_and_reopen_pattern<open_behaviour_e::CLOSE_FIRST_IF_ALREADY_OPEN>(&open_config, &settings);
        }

        /// @brief reopen using new settings (reopen will close first if already open - use open if not desired)
        ///
        /// @param    settings: new connection specific open settings to use
        /// @return   status_e: the final status of the operation
        status_e reopen(opts settings)
        {
            return open_and_reopen_pattern<open_behaviour_e::CLOSE_FIRST_IF_ALREADY_OPEN>(nullptr, &settings);
        }

        /// @brief open using new settings (open will fail if already open - use reopen if not desired), with
        /// additional abstract open configurations
        ///
        /// @param    settings: new open settings to use
        /// @param    open_config: abstract open configuration settings
        /// @return   status_e: the final status of the operation
        status_e open(opts settings, common_opts open_config)
        {
            return open_and_reopen_pattern<open_behaviour_e::FAIL_TO_OPEN_IF_ALREADY_OPEN>(&open_config, &settings);
        }

        /// @brief open using new settings (open will fail if already open - use reopen if not desired), with
        /// additional abstract open configurations
        ///
        /// @param    open_config: abstract open configuration settings
        /// @param    settings: new open settings to use
        /// @return   status_e: the final status of the operation
        status_e open(common_opts open_config, opts settings)
        {
            return open_and_reopen_pattern<open_behaviour_e::FAIL_TO_OPEN_IF_ALREADY_OPEN>(&open_config, &settings);
        }

        /// @brief open using new settings (open will fail if already open - use reopen if not desired)
        ///
        /// @param    settings: new open settings to use
        /// @return   status_e: the final status of the operation
        status_e open(opts settings)
        {
            return open_and_reopen_pattern<open_behaviour_e::FAIL_TO_OPEN_IF_ALREADY_OPEN>(nullptr, &settings);
        }

        status_e close(std::chrono::steady_clock::time_point end_time) final
        {
            close_op op_data{{end_time, status_e::START_NEW_OP}};
            status_e result = transact_operation(end_time, backend::op_category_e::CLOSE, &op_data);
            if (result != status_e::SUCCESS)
                return result;
            return op_data.status;
        }
        status_e close() final
        {
            return close(overflow_safe::now_plus(std::chrono::nanoseconds(default_timeouts::close_timeout_ns)));
        }

        using abstract::receive;
        recv_ret receive(uint8_t *const data, size_t size, std::chrono::steady_clock::time_point end_time) final
        {
            recv_op op_data{{end_time, status_e::START_NEW_OP}, weak_const<size_t>{size}, weak_const<uint8_t *>{data}, 0, default_unset_channel};
            status_e result = transact_operation(end_time, backend::op_category_e::RECEIVE, &op_data);
            if (result != status_e::SUCCESS)
                return {result, 0u};
            return {op_data.status, op_data.received_size, op_data.received_channel};
        }
        recv_ret receive(uint8_t *const data, size_t size) final
        {
            return receive(data, size, overflow_safe::now_plus(std::chrono::nanoseconds(default_timeouts::recv_timeout_ns)));
        }

        using abstract::send;
        status_e send(int channel, const uint8_t *const data, size_t size, std::chrono::steady_clock::time_point end_time) final
        {
            send_op op_data{{end_time, status_e::START_NEW_OP}, size, data, channel};
            status_e result = transact_operation(end_time, backend::op_category_e::SEND, &op_data);
            if (result != status_e::SUCCESS)
                return result;
            return op_data.status;
        }
        status_e send(int channel, const uint8_t *const data, size_t size) final
        {
            return send(channel, data, size, overflow_safe::now_plus(std::chrono::nanoseconds(default_timeouts::send_timeout_ns)));
        }

        [[nodiscard]] bool is_open() const final
        {
#if CPPTXRX_THREADSAFE
            std::lock_guard<std::mutex> lk(m);
#endif
            return m_open_status == status_e::SUCCESS;
        }

        [[nodiscard]] status_e open_status() const final
        {
#if CPPTXRX_THREADSAFE
            std::lock_guard<std::mutex> lk(m);
#endif
            return m_open_status;
        }

        /// @brief Get a copy of the last open opts (even after close is run)
        ///
        /// @param    out_opts: the output opts
        /// @return   true: if there were open arguments to return
        /// @return   false: if "open" or "set_open_args" has never been run
        [[nodiscard]] bool get_open_args(opts &out_opts) const
        {
#if CPPTXRX_THREADSAFE
            std::lock_guard<std::mutex> open_opts_lk(m_open_opts_mutex);
#endif
            if (m_open_opts_initialized)
            {
                out_opts = m_open_opts;
                return true;
            }
            return false;
        }

        /// @brief Set the open opts to be used the next time open or reopen is called without arguments
        /// in case you want to assign opts but defer the actual opening
        ///
        /// @param    new_opts: the new open opts to apply
        template <typename T>
        void set_open_args(T &&new_opts)
        {
#if CPPTXRX_THREADSAFE
            std::lock_guard<std::mutex> open_opts_lk(m_open_opts_mutex);
#endif
            m_open_opts_initialized = true;
            m_open_opts             = new_opts;
        }

        [[nodiscard]] virtual const char *name() const override { return "unnamed"; }
        [[nodiscard]] virtual int id() const override { return -1; }

        /// @brief a constexpr attribute that will be true if the interface is threadsafe
        /// use is_threadsafe() instead if you want a runtime polymorphic compatible attribute
        static constexpr bool threadsafe = CPPTXRX_THREADSAFE;

        bool is_threadsafe() const noexcept final { return threadsafe; }

    protected:
        /// @brief [[OPTIONAL]] Define how to construct your interface. Will only be called once.
        /// Note that unlike the base-class constructor, this constructor will be called in the same
        /// management thread as all other override methods.
        /// Make sure to use this instead of a regular constructor in your derived interface class
        /// if you need one to preserve thread-safety.
        virtual void construct() {}

        /// @brief [[OPTIONAL]] Define how to destruct your interface. Will only be called once.
        /// Note that unlike the base-class destructor, this destructor will be called in the same
        /// management thread as all other override methods.
        /// Make sure to use this instead of a regular destructor in your derived interface class
        /// if you need one to preserve thread-safety.
        virtual void destruct() {}

        /// @brief [[REQUIRED]] Meant to handle the close operation, held in the "transactions.p_close_op"
        /// variable, which is guaranteed to never nullptr in the method.
        virtual void process_close() = 0;

        /// @brief [[REQUIRED]] Meant to handle the open operation, held in the "transactions.p_open_op"
        /// variable, which is guaranteed to never nullptr in the method.
        virtual void process_open() = 0;

        /// @brief [[REQUIRED]] Meant to handle a send operation, a receive operation, both simultaneously, or any other
        /// persistent maintenance operations using the "transactions.idle_in_send_recv" option.
        /// The "transactions.p_send_op", and "transactions.p_receive_op" pointers are not nullptr when
        /// their operation is requested.
        virtual void process_send_receive() = 0;

#if CPPTXRX_THREADSAFE
        /// @brief [[only REQUIRED for interface::threadsafe]]
        /// Used to wake up a process_open/close/send_receive call that is blocking when another operation is requested.
        /// WARNING!: wake_process is the only "overridden" method that can be called from other threads.
        /// WARNING!: The wake signal must be sticky (like a eventfd object), since there's no guarantee that wake_process
        ///           will be called precisely when your process_ method is performing a block or reading the wake signal.
        virtual void wake_process() = 0;
#endif

        using transactions_args<opts>::transactions;
        using transactions_args<opts>::m_open_status;
        using transactions_args<opts>::m_open_opts;

    private:
#if CPPTXRX_THREADSAFE
        void safe_wake_process(std::unique_lock<std::mutex> &lk)
        {
            wait_for_constructed(lk); // can't call wake_process before constructed

            // the destroy operation, won't actually be able to be started or completed until the mutex is release so it's safe
            // to check active_ops and then call wake_process now in case any of the process_<open/close/send/receive> methods
            // needs waking, without worrying about the destructor being called first
            if (!active_ops.is_complete(interface::backend::op_category_e::DESTROY))
                wake_process();
        }
#endif
        /// @brief last common non-connection-specific options passed into an open() call.
        /// Note that unlike m_open_opts, these should not be freely modified and instead should be changed via open() or reopen(),
        /// since they're only used by the wrapper code.
        common_opts m_common_open_opts{};

        /// @brief Extra memory needed for auto reopen requesting.
        open_op m_open_op{};

        enum class open_behaviour_e
        {
            CLOSE_FIRST_IF_ALREADY_OPEN,
            FAIL_TO_OPEN_IF_ALREADY_OPEN
        };

        template <open_behaviour_e open_behaviour_value>
        status_e open_and_reopen_pattern(common_opts *p_open_config, opts *p_settings)
        {
            open_op op_data;
            if (p_open_config == nullptr)
            {
#if CPPTXRX_THREADSAFE
                std::lock_guard<std::mutex> open_opts_lk(m_open_opts_mutex);
                // note: no need to check if the m_common_open_opts is initialized, since if it isn't the default will be used
#endif
                op_data.end_time = m_common_open_opts.resolve_open_timeout(std::chrono::nanoseconds(default_timeouts::open_timeout_ns));
            }
            else
            {
                op_data.end_time = p_open_config->resolve_open_timeout(std::chrono::nanoseconds(default_timeouts::open_timeout_ns));
            }
            internal_open_op all_op_data{&op_data, p_open_config, p_settings, open_behaviour_value};
            status_e result = transact_operation(op_data.end_time, backend::op_category_e::OPEN, &all_op_data);
            if (result != status_e::SUCCESS)
                return result;
            return op_data.status;
        }

        // tracks multiple simultaneous operations as well as their progress in a single bitpacked integer
        // so that reads and writes can be fast/simple bitshift/mask operations
        backend::op_bitmasks active_ops{};

        // allow open and reopen to be called immediately without any opts if opts == no_opts
        bool m_open_opts_initialized{std::is_same<opts, no_opts>::value};

        // this pointer is used alongside ext_p_open_common_opts, in order to convey any additional connection specific new open options:
        // * ext_p_open_opts conveys new (or the last) opts(), which are connection-specific options, such as: TCP/IP ports and address
        // * ext_p_open_common_opts conveys new common_opts(), which are common open options, such as: timeouts, filters, and callbacks
        // * ext_p_open_op conveys the common operation data
        opts *ext_p_open_opts{nullptr};
        common_opts *ext_p_open_common_opts{nullptr};

        // these allow the op pointers in "transactions" to be set externally
        send_op *ext_p_send_op{nullptr};
        recv_op *ext_p_recv_op{nullptr};
        open_op *ext_p_open_op{nullptr};
        close_op *ext_p_close_op{nullptr};

#if CPPTXRX_THREADSAFE
        // holds onto the receive callback pointer as a shared_ptr object, so that the underlying pointer
        // can either be a user-managed memory object that is allocated not-on the heap, or if the user
        // uses allow_heap() this can be a locally managed heap memory pointer. The difference between
        // the two is simply whether or not the deletion function attached will call delete (for the heap),
        // or does nothing (for the user-managed pointer).
        // Note: This feature is only available in the threadsafe mode, since otherwise other threads
        // wouldn't be able to get to it in order to stop the loop of receiving. For the raw interface,
        // the user must just call receive() in a loop instead, since then they can define an exit condition.
        std::shared_ptr<receive_callback::abstract> p_rx_callback = nullptr;
#endif

        // holds onto the receive filter pointer (user-managed by default, or heap managed if allow_heap() was used)
        // as well as all additional meta-data needed to process that filter
        filter_processor<false> rx_filter_mananger = {};

        // holds onto the sending filter pointer (user-managed by default, or heap managed if allow_heap() was used)
        // as well as all additional meta-data needed to process that filter
        filter_processor<true> tx_filter_mananger = {};

#if CPPTXRX_THREADSAFE

        /// if the connection fails to open, or closes automatically due to an error, setting this parameter to a
        /// >= 0 value will tell the connection to just keep calling reopen automatically after the provided interval
        /// until the user manually calls .close() or the class destruction is started. Note that a negative value
        /// will disable the setting - and reopens will no longer occur automatically. Defaults to -1 (disabled).
        std::chrono::nanoseconds auto_reopen_after = std::chrono::nanoseconds(-1);
        // allows the auto_reopen_after setting to be disabled (without changing its value)
        // if a manual close() has occurred, indicating it should stay closed.
        bool allow_auto_reopening = false;

        // a dedicated mutex just used for the m_open_opts member variable, that way the m_open_opts can be set and
        // get in a polled manner, independently of the normal active_ops operation passing.
        mutable std::mutex m_open_opts_mutex{};

        // the main mutex for all the ext pointer passing, cv usage, and active_ops manipulation
        mutable std::mutex m{};
        // the main condition_variable for all the ext pointer passing, cv usage, and active_ops manipulation
        mutable std::condition_variable cv{};
        // the main operations management thread for the threadsafe variant, responsible for all low level API calls:
        //     constructing, operating (open/close/send/receive), and destructing
        raii_thread thread_handle{};
#endif

        struct internal_open_op
        {
            open_op *p_open_op_data;
            common_opts *p_common_opts_open_args;
            opts *p_opts_open_args;
            open_behaviour_e open_behaviour;
        };

        inline static void flip_status_from_started_to_in_progress(common_op *op_ptr)
        {
            if (op_ptr != nullptr && op_ptr->status == status_e::START_NEW_OP)
                op_ptr->status = status_e::OP_IN_PROGRESS;
        }

#if CPPTXRX_THREADSAFE
        inline bool check_and_process_destruction_in_single_op(std::unique_lock<std::mutex> &lk)
        {
            if (active_ops.is_requested(backend::op_category_e::DESTROY))
            {
                active_ops.accept_request(backend::op_category_e::DESTROY);
                m_open_status = status_e::NOT_OPEN;
                lk.unlock();
                cv.notify_all();
                return true;
            }
            return false;
        }
#endif

        bool single_operation()
        {
            // wait for a new transaction instruction
            {
#if CPPTXRX_THREADSAFE
                std::unique_lock<std::mutex> lk(m);

                if (check_and_process_destruction_in_single_op(lk))
                    return false;

                // if using the receive callback, then a new recv operation must be
                // initiated automatically whenever possible here:
                if (p_rx_callback != nullptr && ext_p_recv_op == nullptr)
                {
                    auto &rx_callback = *p_rx_callback;

                    // if the operation is completed (or errored out)
                    if (!rx_callback.op_data.status.is_operating())
                    {
                        // if the receive ended because the socket is simply not open, don't bother calling the receive callback
                        if (rx_callback.op_data.status != status_e::NOT_OPEN)
                        {
                            // run the callback function outside of the mutex lock
                            lk.unlock();
                            rx_callback(rx_callback.op_data);
                            lk.lock();

                            if (check_and_process_destruction_in_single_op(lk))
                                return false;
                        }

                        // and then reset the op_data for next time
                        rx_callback.reset_all();
                    }

                    // re-request a new receive operation using the callback's op_data settings
                    // only if there isn't a close or open operation pending, and the socket is open
                    if (ext_p_close_op == nullptr && ext_p_open_op == nullptr && m_open_status == status_e::SUCCESS)
                        ext_p_recv_op = &rx_callback.op_data;
                }

                const bool no_active_transactions = ext_p_send_op == nullptr &&
                                                    ext_p_recv_op == nullptr &&
                                                    ext_p_close_op == nullptr &&
                                                    ext_p_open_op == nullptr;

                if (no_active_transactions)
                {
                    // handle automatic reopening
                    if (allow_auto_reopening &&
                        m_open_status != status_e::SUCCESS &&
                        m_open_opts_initialized &&
                        auto_reopen_after >= std::chrono::nanoseconds(0))
                    {
                        // wait for the reopen duration (unless another request comes in - only an open or destroy would be allowed anyway)
                        cv.wait_until(lk, overflow_safe::now_plus(auto_reopen_after), [this]()
                                      { return active_ops.is_any(backend::op_bitmasks::ANY_REQUEST); });

                        if (check_and_process_destruction_in_single_op(lk))
                            return false;

                        // if there was no other operation
                        if (!active_ops.is_any(backend::op_bitmasks::ANY_REQUEST))
                        {
                            // request a reopen:
                            std::lock_guard<std::mutex> open_opts_lk(m_open_opts_mutex);

                            // use the last open call's "opts()" and "common_opts()"
                            ext_p_open_opts        = &m_open_opts;
                            ext_p_open_common_opts = &m_common_open_opts;
                            ext_p_open_op          = &m_open_op;

                            // update the end time and status
                            m_open_op.end_time = m_common_open_opts.resolve_open_timeout(std::chrono::nanoseconds(default_timeouts::open_timeout_ns));
                            m_open_op.status   = status_e::START_NEW_OP;

                            // this prevents multiple simultaneous open requests, but does then require the user to call close() while autoopen is used
                            active_ops.start_request(backend::op_category_e::OPEN);
                        }
                    }
                    // otherwise, wait for new requests if not idling in send/recv while open
                    else if (!(transactions.idle_in_send_recv && m_open_status == status_e::SUCCESS))
                    {
                        cv.wait(lk, [this]()
                                { return active_ops.is_any(backend::op_bitmasks::ANY_REQUEST); });

                        if (check_and_process_destruction_in_single_op(lk))
                            return false;
                    }
                }
#endif

                if (tx_filter_mananger)
                {
                    if (tx_filter_mananger.process_sending_filter(ext_p_send_op, transactions.p_send_op))
                    {
                        active_ops.complete_request(backend::op_category_e::SEND);
#if CPPTXRX_THREADSAFE
                        // notify caller that their transaction was accepted
                        lk.unlock();
                        cv.notify_all();
#endif
                        // and skip to the next loop iteration immediately
                        return true;
                    }
                }
                else if (ext_p_send_op != transactions.p_send_op)
                {
                    // by doing the swapping of transaction pointers outside here in the mutex lock, we can make sure
                    // that no operations can update while the process_<open/close/send_receive> methods are active.
                    assert(transactions.p_send_op == nullptr);
                    transactions.p_send_op = ext_p_send_op;
                }

                if (rx_filter_mananger)
                {
                    if (rx_filter_mananger.process_receiving_filter(ext_p_recv_op, transactions.p_recv_op))
                    {
#if CPPTXRX_THREADSAFE
                        // if the receive was not initiated by a callback then notify the external world
                        if (p_rx_callback == nullptr)
                        {
                            active_ops.complete_request(backend::op_category_e::RECEIVE);
                            // notify caller that their transaction was accepted
                            lk.unlock();
                            cv.notify_all();
                        }
#else
                        // in non-threadsafe mode, the receive cannot be initiated by a callback, so it
                        // should always be completed
                        active_ops.complete_request(backend::op_category_e::RECEIVE);
#endif

                        // either way, skip to the next loop iteration immediately
                        return true;
                    }
                }
                else if (ext_p_recv_op != transactions.p_recv_op)
                {
                    assert(transactions.p_recv_op == nullptr);
                    transactions.p_recv_op = ext_p_recv_op;
                }

                if (ext_p_open_op != transactions.p_open_op)
                {
                    assert(transactions.p_open_op == nullptr);
                    transactions.p_open_op = ext_p_open_op;
                }
                if (ext_p_close_op != transactions.p_close_op)
                {
                    assert(transactions.p_close_op == nullptr);
                    transactions.p_close_op = ext_p_close_op;
                }

                if (active_ops.is_any(backend::op_bitmasks::ANY_REQUEST))
                {
                    if (active_ops.is_requested(backend::op_category_e::SEND))
                        active_ops.accept_request(backend::op_category_e::SEND);
                    if (active_ops.is_requested(backend::op_category_e::RECEIVE))
                        active_ops.accept_request(backend::op_category_e::RECEIVE);
                    if (active_ops.is_requested(backend::op_category_e::CLOSE))
                    {
#if CPPTXRX_THREADSAFE
                        allow_auto_reopening = false;
#endif
                        active_ops.accept_request(backend::op_category_e::CLOSE);
                    }
                    if (active_ops.is_requested(backend::op_category_e::OPEN))
                    {
                        // save these new open settings, regardless of if they weill be successful later, to enable retries
                        {
#if CPPTXRX_THREADSAFE
                            allow_auto_reopening = true;
                            std::lock_guard<std::mutex> open_opts_lk(m_open_opts_mutex);
#endif
                            m_open_opts_initialized = true;
                            if (&m_open_opts != ext_p_open_opts && ext_p_open_opts != nullptr)
                                m_open_opts = *ext_p_open_opts;
                            if (&m_common_open_opts != ext_p_open_common_opts && ext_p_open_common_opts != nullptr)
                                m_common_open_opts = *ext_p_open_common_opts;
                            if (&m_open_op != ext_p_open_op && ext_p_open_op != nullptr)
                                m_open_op = *ext_p_open_op;
                        }
                        active_ops.accept_request(backend::op_category_e::OPEN);
                    }
                }
            }

#if CPPTXRX_THREADSAFE
            // notify caller that their transaction was accepted
            cv.notify_all();
#endif

            // do the transaction(s)
            {
                // only do one operation at a time in order to quickly notify/wake the calling method
                // prioritizing closing --> then opening --> and then send/receiving
                if (transactions.p_close_op != nullptr)
                {
                    process_close();
                    flip_status_from_started_to_in_progress(transactions.p_close_op);
                }
                else if (transactions.p_open_op != nullptr)
                {
#if CPPTXRX_THREADSAFE
                    std::lock_guard<std::mutex> open_opts_lk(m_open_opts_mutex);
#endif
                    process_open();
                    flip_status_from_started_to_in_progress(transactions.p_open_op);
                }
                else if (transactions.p_send_op != nullptr || transactions.p_recv_op != nullptr || transactions.idle_in_send_recv)
                {
                    process_send_receive();
                    flip_status_from_started_to_in_progress(transactions.p_send_op);
                    flip_status_from_started_to_in_progress(transactions.p_recv_op);
                }
            }

            // check all the transaction return status values, to see if any transactions timed out or finished
            end_transaction(transactions.p_send_op, backend::op_category_e::SEND);
            end_transaction(transactions.p_recv_op, backend::op_category_e::RECEIVE);
            end_transaction(transactions.p_open_op, backend::op_category_e::OPEN);
            end_transaction(transactions.p_close_op, backend::op_category_e::CLOSE);
            return true;
        }

        void end_transaction(common_op *op_ptr, backend::op_category_e op_ptr_type)
        {
            if (op_ptr == nullptr)
                return;

            if (op_ptr->is_operating())
            {
                // check for a timeout
                if (op_ptr->end_time < std::chrono::steady_clock::now())
                    op_ptr->status = status_e::TIMED_OUT;

                // check for the user closing the connection, canceling all other normal non-open operations
                // this is safe to do without a mutex lock because only the current thread can modify m_open_status
                else if (m_open_status != status_e::SUCCESS && (op_ptr_type == backend::op_category_e::SEND ||
                                                                op_ptr_type == backend::op_category_e::RECEIVE ||
                                                                op_ptr_type == backend::op_category_e::CLOSE))
                    op_ptr->status = status_e::NOT_OPEN;

                // otherwise
                else
                    return; // wait until the "is_operating()" status is finished
            }

            // if not "is_operating()", then end the transaction
            {
#if CPPTXRX_THREADSAFE
                std::lock_guard<std::mutex> lk(m);
#endif
                bool complete_operation = true;
                switch (op_ptr_type)
                {
                case backend::op_category_e::SEND:
                {
                    if (tx_filter_mananger)
                        return; // don't do anything further (since requests are handled by the filter manager)
                    transactions.p_send_op = nullptr;
                    ext_p_send_op          = nullptr;
                    break;
                }
                case backend::op_category_e::RECEIVE:
                {
                    if (rx_filter_mananger)
                        return; // don't do anything further (since requests are handled by the filter manager)
#if CPPTXRX_THREADSAFE
                    complete_operation = p_rx_callback == nullptr;
#endif
                    transactions.p_recv_op = nullptr;
                    ext_p_recv_op          = nullptr;
                    break;
                }
                case backend::op_category_e::OPEN:
                {
#if CPPTXRX_THREADSAFE
                    std::lock_guard<std::mutex> open_opts_lk(m_open_opts_mutex);
                    // install any new auto_reopen_after setting - even if the open failed, so that retries can occur
                    if (m_common_open_opts.m_auto_reopen_after.has_update())
                        auto_reopen_after = m_common_open_opts.m_auto_reopen_after.value;
                    const bool auto_reopen_enabled = auto_reopen_after >= std::chrono::nanoseconds(0);
#else
                    constexpr bool auto_reopen_enabled = false;
#endif

                    // apply all other settings only if the open was successful, or if auto-reopens are enabled
                    if (transactions.p_open_op->status == status_e::SUCCESS || auto_reopen_enabled)
                    {
#if CPPTXRX_THREADSAFE
                        // after the open operation, update any m_callback functions
                        // by setting the p_rx_callback, and disabling or enabling external receive ops
                        if (m_common_open_opts.m_callback.has_update())
                        {
                            auto &ptr = m_common_open_opts.m_callback.value;
                            if (ptr == nullptr || !ptr->is_valid())
                            {
                                // disable callbacks and enable external receives
                                p_rx_callback = nullptr;
                                active_ops.enable_op(backend::op_category_e::RECEIVE);
                            }
                            else
                            {
                                // enable callbacks and disable external receives
                                p_rx_callback = ptr;
                                active_ops.disable_op(backend::op_category_e::RECEIVE);
                            }
                        }
#endif

                        // install any new receiving filter
                        if (m_common_open_opts.m_rx_filter.has_update())
                            rx_filter_mananger.replace_filter(m_common_open_opts.m_rx_filter.value);

                        // install any new send filter
                        if (m_common_open_opts.m_tx_filter.has_update())
                            tx_filter_mananger.replace_filter(m_common_open_opts.m_tx_filter.value);
                    }

                    m_open_status          = transactions.p_open_op->status;
                    transactions.p_open_op = nullptr;
                    ext_p_open_op          = nullptr;
                    ext_p_open_common_opts = nullptr;
                    ext_p_open_opts        = nullptr;
                    break;
                }
                case backend::op_category_e::CLOSE:
                {
                    if (transactions.p_close_op->status == status_e::SUCCESS && m_open_status == status_e::SUCCESS)
                        m_open_status = status_e::NOT_OPEN;
                    transactions.p_close_op = nullptr;
                    ext_p_close_op          = nullptr;
                    break;
                }
                case backend::op_category_e::CONSTRUCT:
                    assert(false); // should never get here
                    break;
                case backend::op_category_e::DESTROY:
                    assert(false); // should never get here
                    break;
                default:
                    assert(false); // should never get here
                    break;
                }

                if (complete_operation)
                    active_ops.complete_request(op_ptr_type);
            }
#if CPPTXRX_THREADSAFE
            cv.notify_all();
#endif
        }

#if CPPTXRX_THREADSAFE
        inline void mark_as_constructed()
        {
            {
                std::unique_lock<std::mutex> lk(m);
                active_ops.complete_request(backend::op_category_e::CONSTRUCT);
            }
            cv.notify_all();
        }
        inline void wait_for_constructed(std::unique_lock<std::mutex> &lk)
        {
            if (!active_ops.is_complete(backend::op_category_e::CONSTRUCT))
            {
                lk.unlock();
                cv.notify_all();
                lk.lock();
                cv.wait(lk, [&]() { // don't allow any operations if not constructed
                    return active_ops.is_complete(backend::op_category_e::CONSTRUCT);
                });
            }
        }
#endif
        status_e transact_operation(std::chrono::steady_clock::time_point end_time, backend::op_category_e op, void *op_src_data)
        {
            // wait for a transaction request slot to be available
            {
#if CPPTXRX_THREADSAFE
                std::unique_lock<std::mutex> lk(m);
                const bool timed_out = !cv.wait_until(
                    lk, end_time,
                    [&]()
                    {
                        // don't allow any other transactions if we're in the
                        // process of destructing
                        if (active_ops.is_any(backend::op_bitmasks::ANY_DESTROY) || active_ops.is_disabled(op))
                            return true;

                        // don't bother waiting for a send/receive/close if the socket isn't even open
                        if (m_open_status != status_e::SUCCESS && (op == backend::op_category_e::RECEIVE ||
                                                                   op == backend::op_category_e::SEND ||
                                                                   op == backend::op_category_e::CLOSE))
                            return true;

                        // keep waiting if in the process of opening, or closing
                        if (active_ops.is_any(backend::op_bitmasks::ANY_OPEN_OR_CLOSE))
                            return false;

                        // stop waiting if there is not already an operation pending
                        // of the requested op category
                        return !active_ops.is_any(op);
                    });
                if (timed_out)
                    return status_e::TIMED_OUT;
#endif
                if (active_ops.is_any(backend::op_bitmasks::ANY_DESTROY))
                    return status_e::CANCELED_IN_DESTROY;

                if (active_ops.is_disabled(op))
                    return status_e::DISABLED;

                // mark the operation request as started and populate any needed op data
                switch (op)
                {
                case backend::op_category_e::RECEIVE:
                {
                    if (m_open_status != status_e::SUCCESS)
                        return status_e::NOT_OPEN;
                    ext_p_recv_op = reinterpret_cast<recv_op *>(op_src_data);
                    break;
                }
                case backend::op_category_e::SEND:
                {
                    if (m_open_status != status_e::SUCCESS)
                        return status_e::NOT_OPEN;
                    ext_p_send_op = reinterpret_cast<send_op *>(op_src_data);
                    break;
                }
                case backend::op_category_e::CLOSE:
                {
                    if (m_open_status != status_e::SUCCESS)
                        return status_e::NOT_OPEN;
                    ext_p_close_op = reinterpret_cast<close_op *>(op_src_data);
                    break;
                }
                case backend::op_category_e::OPEN:
                {
                    internal_open_op &op_src_data_ref = *reinterpret_cast<internal_open_op *>(op_src_data);
                    if (op_src_data_ref.open_behaviour == open_behaviour_e::FAIL_TO_OPEN_IF_ALREADY_OPEN && m_open_status == status_e::SUCCESS)
                        return status_e::FAILED_ALREADY_OPEN;

                    // if no connection specific options can be re-used, fail immediately with an error so that no nullptr options can be passed in
                    if (op_src_data_ref.p_opts_open_args == nullptr)
                    {
#if CPPTXRX_THREADSAFE
                        std::lock_guard<std::mutex> open_opts_lk(m_open_opts_mutex);
#endif
                        if (!m_open_opts_initialized)
                            return status_e::NO_PRIOR_OPEN_ARGS;
                    }

                    // check for invalid arguments in order to immediately error out if possible
                    if (op_src_data_ref.p_common_opts_open_args != nullptr)
                    {
#if CPPTXRX_THREADSAFE
                        if (op_src_data_ref.p_common_opts_open_args->m_callback.has_update())
                        {
                            auto &ptr = op_src_data_ref.p_common_opts_open_args->m_callback.value;
                            if (ptr != nullptr && !ptr->is_valid())
                            {
                                m_open_status = status_e::INVALID_ARG_RECV_CALLBACK_FUNC;
                                return status_e::INVALID_ARG_RECV_CALLBACK_FUNC;
                            }
                        }
#else
                        if (op_src_data_ref.p_common_opts_open_args->m_callback.has_update() &&
                            op_src_data_ref.p_common_opts_open_args->m_callback.value != nullptr)
                        {
                            m_open_status = status_e::RECV_CALLBACK_NOT_VALID_IN_RAW;
                            return status_e::RECV_CALLBACK_NOT_VALID_IN_RAW;
                        }
#endif
                    }

                    // if no common_opts were specified, try and re-use the last common_opts options
                    if (op_src_data_ref.p_common_opts_open_args == nullptr)
                        ext_p_open_common_opts = &m_common_open_opts;
                    else
                        ext_p_open_common_opts = op_src_data_ref.p_common_opts_open_args;

                    // if no opts were specified, try and re-use the last opts options
                    if (op_src_data_ref.p_opts_open_args == nullptr)
                        ext_p_open_opts = &m_open_opts;
                    else
                        ext_p_open_opts = op_src_data_ref.p_opts_open_args;

                    ext_p_open_op = op_src_data_ref.p_open_op_data;
                    break;
                }
                case backend::op_category_e::CONSTRUCT:
                    // fallthrough
                case backend::op_category_e::DESTROY:
                    // fallthrough
                default:
                    return status_e::CANCELED_IN_DESTROY;
                }

                active_ops.start_request(op);
#if CPPTXRX_THREADSAFE
                safe_wake_process(lk);
#endif
            }

#if CPPTXRX_THREADSAFE
            cv.notify_all();
#else
            single_operation();
#endif

            // wait for the operation loop to respond with a completion status and release the op request
            {
#if CPPTXRX_THREADSAFE
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk,
                        [this, op]()
                        {
                            // note that DESTROY_ACCEPT_OR_COMPLETED is used instead of ANY_DESTROY, so that
                            // the active op pointer is kept alive during the destruction process. Otherwise
                            // ANY_DESTROY might return on a destruction request that hasn't been accepted yet,
                            // in which case this function would return and the memory to the active operation
                            // that is creates would also go out of scope, creating a hard to debug memory
                            // corruption error when the pointer is used during the destruction process.
                            return active_ops.is_any(backend::op_bitmasks::DESTROY_ACCEPT_OR_COMPLETED) ||
                                   active_ops.is_complete(op);
                        });
#endif
                if (!active_ops.is_complete(op))
                    return status_e::CANCELED_IN_DESTROY;
                active_ops.end_request(op);
            }
#if CPPTXRX_THREADSAFE
            cv.notify_all();
#endif
            return status_e::SUCCESS;
        }
    };

} // namespace interface

#endif // CPPTXRX_FACTORY_H_