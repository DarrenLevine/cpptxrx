#ifndef CPPTXRX_FACTORY_H_
#define CPPTXRX_FACTORY_H_

#include "cpptxrx_abstract.h"
#include "cpptxrx_macros.h"
#include "cpptxrx_op_backend.h"

#ifdef CPPTXRX_CLASS_NAME
#undef CPPTXRX_CLASS_NAME
#endif
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
    /// @tparam   default_recv_timeout_ns (optional): default recv timeout in ns
    /// @tparam   default_send_timeout_ns (optional): default send timeout in ns
    /// @tparam   default_open_timeout_ns (optional): default open timeout in ns
    /// @tparam   default_clse_timeout_ns (optional): default close timeout in ns
    template <typename open_opts_type,
              uint64_t default_recv_timeout_ns,
              uint64_t default_send_timeout_ns,
              uint64_t default_open_timeout_ns,
              uint64_t default_clse_timeout_ns>
    class CPPTXRX_CLASS_NAME : protected transactions_args<open_opts_type>, public abstract
    {
    public:
        /// @brief options type to use when calling "open"
        using opts = open_opts_type;

        /// @brief Construct a new cpptxrx interfaces object, and if running in thread safe mode, spools
        /// up a management thread where all interface interactions will occur, and that all other methods
        /// will simply dispatch requests to.
        CPPTXRX_CLASS_NAME()
        {
#if CPPTXRX_THREADSAFE
            // must construct the thread after the full memory initialization, since a race condition can occur if
            // the thread is created in the initializer list, due to the virtual table not being initialized yet
            thread_handle = raii_thread(
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
                });
#else
            // NOTE: there cannot be a virtual construct() called here, since the child class won't exist yet
            // so instead the child class is required to use IMPORT_CPPTXRX_CTOR_AND_DTOR to call its own construct() method
#endif
        }

        /// @brief Destroy the cpptxrx interface object by either requesting a destroy from the
        /// threadsafe manager, or just calling destruct() directly if not threadsafe
        virtual ~CPPTXRX_CLASS_NAME()
        {
            destroy();
        }

        CPPTXRX_CLASS_NAME operator=(const CPPTXRX_CLASS_NAME &) = delete;
        CPPTXRX_CLASS_NAME(const CPPTXRX_CLASS_NAME &)           = delete;

        void destroy() override
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
                wait_for_constructed(lk); // can't call wake_process before constructed

                // the destroy operation, won't actually be able to be started or completed until the mutex is release so it's safe
                // to check active_ops and then call wake_process now in case any of the process_<open/close/send/receive> methods
                // needs waking, without worrying about the destructor being called first
                if (!active_ops.is_complete(interface::backend::op_category_e::DESTROY))
                    wake_process();
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

        // Various open methods, letting you pick what happens if open is called on an already open connection, and pick whether to reuse settings
        // NOTE: Detailed documentation is in cpptxrx_abstract.h for all "override" methods (the methods that don't use "opts")
        //       Only the methods that are not overrides are documented here.

        status_e reopen(std::chrono::steady_clock::time_point end_time) override
        {
            open_op op_data{end_time, status_e::IN_PROGRESS};
            internal_open_op all_op_data{&op_data, nullptr, open_behaviour_e::CLOSE_FIRST_IF_ALREADY_OPEN};
            status_e result = transact_operation(end_time, backend::op_category_e::OPEN, &all_op_data);
            if (result != status_e::SUCCESS)
                return result;
            return op_data.status;
        }
        status_e reopen(std::chrono::nanoseconds timeout) override
        {
            return reopen(std::chrono::steady_clock::now() + timeout);
        }
        status_e reopen() override
        {
            return reopen(std::chrono::steady_clock::now() + std::chrono::nanoseconds(default_open_timeout_ns));
        }
        status_e open(std::chrono::steady_clock::time_point end_time) override
        {
            open_op op_data{end_time, status_e::IN_PROGRESS};
            internal_open_op all_op_data{&op_data, nullptr, open_behaviour_e::FAIL_TO_OPEN_IF_ALREADY_OPEN};
            status_e result = transact_operation(end_time, backend::op_category_e::OPEN, &all_op_data);
            if (result != status_e::SUCCESS)
                return result;
            return op_data.status;
        }
        status_e open(std::chrono::nanoseconds timeout) override
        {
            return open(std::chrono::steady_clock::now() + timeout);
        }
        status_e open() override
        {
            return open(std::chrono::steady_clock::now() + std::chrono::nanoseconds(default_open_timeout_ns));
        }

        /// @brief reopen using new settings (reopen will close first if already open - use open if not desired), with a absolute timeout
        ///
        /// @param    settings: new open settings to use
        /// @param    end_time: when to time out the operation
        /// @return   status_e: the final status of the operation
        status_e reopen(opts settings, std::chrono::steady_clock::time_point end_time)
        {
            open_op op_data{end_time, status_e::IN_PROGRESS};
            internal_open_op all_op_data{&op_data, &settings, open_behaviour_e::CLOSE_FIRST_IF_ALREADY_OPEN};
            status_e result = transact_operation(end_time, backend::op_category_e::OPEN, &all_op_data);
            if (result != status_e::SUCCESS)
                return result;
            return op_data.status;
        }

        /// @brief reopen using new settings (reopen will close first if already open - use open if not desired),
        /// with an optional relative timeout
        ///
        /// @param    settings: new open settings to use
        /// @param    timeout (optional): when to time out the operation
        /// @return   status_e: the final status of the operation
        status_e reopen(opts settings, std::chrono::nanoseconds timeout = std::chrono::nanoseconds(default_open_timeout_ns))
        {
            return reopen(settings, std::chrono::steady_clock::now() + timeout);
        }

        /// @brief open using new settings (open will fail if already open - use reopen if not desired), with a
        /// absolute timeout
        ///
        /// @param    settings: new open settings to use
        /// @param    end_time: when to time out the operation
        /// @return   status_e: the final status of the operation
        status_e open(opts settings, std::chrono::steady_clock::time_point end_time)
        {
            open_op op_data{end_time, status_e::IN_PROGRESS};
            internal_open_op all_op_data{&op_data, &settings, open_behaviour_e::FAIL_TO_OPEN_IF_ALREADY_OPEN};
            status_e result = transact_operation(end_time, backend::op_category_e::OPEN, &all_op_data);
            if (result != status_e::SUCCESS)
                return result;
            return op_data.status;
        }

        /// @brief open using new settings (open will fail if already open - use reopen if not desired), with
        /// an optional relative timeout
        ///
        /// @param    settings: new open settings to use
        /// @param    timeout (optional): when to time out the operation
        /// @return   status_e: the final status of the operation
        status_e open(opts settings, std::chrono::nanoseconds timeout = std::chrono::nanoseconds(default_open_timeout_ns))
        {
            return open(settings, std::chrono::steady_clock::now() + timeout);
        }

        status_e close(std::chrono::steady_clock::time_point end_time) override
        {
            close_op op_data{end_time, status_e::IN_PROGRESS};
            status_e result = transact_operation(end_time, backend::op_category_e::CLOSE, &op_data);
            if (result != status_e::SUCCESS)
                return result;
            return op_data.status;
        }
        status_e close(std::chrono::nanoseconds timeout) override
        {
            return close(std::chrono::steady_clock::now() + timeout);
        }
        status_e close() override
        {
            return close(std::chrono::steady_clock::now() + std::chrono::nanoseconds(default_clse_timeout_ns));
        }

        using abstract::receive;
        recv_ret receive(uint8_t *const data, size_t size, std::chrono::steady_clock::time_point end_time) override
        {
            recv_op op_data{end_time, status_e::IN_PROGRESS, data, size};
            status_e result = transact_operation(end_time, backend::op_category_e::RECEIVE, &op_data);
            if (result != status_e::SUCCESS)
                return {result, 0u};
            return {op_data.status, op_data.returned_recv_size};
        }
        recv_ret receive(uint8_t *const data, size_t size, std::chrono::nanoseconds timeout) override
        {
            return receive(data, size, std::chrono::steady_clock::now() + timeout);
        }
        recv_ret receive(uint8_t *const data, size_t size) override
        {
            return receive(data, size, std::chrono::steady_clock::now() + std::chrono::nanoseconds(default_recv_timeout_ns));
        }

        using abstract::send;
        status_e send(const uint8_t *const data, size_t size, std::chrono::steady_clock::time_point end_time) override
        {
            send_op op_data{end_time, status_e::IN_PROGRESS, data, size};
            status_e result = transact_operation(end_time, backend::op_category_e::SEND, &op_data);
            if (result != status_e::SUCCESS)
                return result;
            return op_data.status;
        }
        status_e send(const uint8_t *const data, size_t size, std::chrono::nanoseconds timeout) override
        {
            return send(data, size, std::chrono::steady_clock::now() + timeout);
        }
        status_e send(const uint8_t *const data, size_t size) override
        {
            return send(data, size, std::chrono::steady_clock::now() + std::chrono::nanoseconds(default_send_timeout_ns));
        }

        [[nodiscard]] bool is_open() const override
        {
#if CPPTXRX_THREADSAFE
            std::lock_guard<std::mutex> lk(m);
#endif
            return m_open_status == status_e::SUCCESS;
        }

        [[nodiscard]] status_e open_status() const override
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

        virtual bool is_threadsafe() const override { return threadsafe; }

    protected:
        /// @brief [[OPTIONAL]] Define how to construct your interface. Will only be called once.
        /// Make sure to use this instead of a constructor, if you need one, so that you can call
        /// virtual methods in this constructor.
        virtual void construct() {}

        /// @brief [[OPTIONAL]] Define how to destruct your interface. Will only be called once.
        virtual void destruct() {}

        /// @brief [[REQUIRED]] Meant to handle the close operation, held in the "transactions.p_close_op"
        /// variable, which is guaranteed to never nullptr in the method.
        virtual void process_close() = 0;

        /// @brief [[REQUIRED]] Meant to handle the open operation, held in the "transactions.p_open_op"
        /// variable, which is guaranteed to never nullptr in the method.
        virtual void process_open() = 0;

        /// @brief [[REQUIRED]] Meant to handle a send operation, a receive operation, or both simultaneously.
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
        using transactions_args<opts>::m_open_opts;
        using transactions_args<opts>::m_open_status;

    private:
        enum class open_behaviour_e
        {
            CLOSE_FIRST_IF_ALREADY_OPEN,
            FAIL_TO_OPEN_IF_ALREADY_OPEN
        };
        backend::op_bitmasks active_ops{};
        // allow open and reopen to be called immediately without any opts if opts == no_opts
        bool m_open_opts_initialized{std::is_same<opts, no_opts>::value};
        opts *p_open_opts{nullptr};

#if CPPTXRX_THREADSAFE
        mutable std::mutex m{};
        mutable std::mutex m_open_opts_mutex{};
        mutable std::condition_variable cv{};
        raii_thread thread_handle{};
#endif

        struct internal_open_op
        {
            open_op *p_open_op_data;
            opts *p_open_arguments;
            open_behaviour_e open_behaviour;
        };

        bool single_operation()
        {
            // wait for a new transaction instruction
            bool destroyed = false;
            {
#if CPPTXRX_THREADSAFE
                std::unique_lock<std::mutex> lk(m);
                bool no_active_transactions = transactions.p_send_op == nullptr &&
                                              transactions.p_recv_op == nullptr &&
                                              transactions.p_close_op == nullptr &&
                                              transactions.p_open_op == nullptr;
                if (no_active_transactions)
                    cv.wait(lk, [this]()
                            { return active_ops.is_any(backend::op_bitmasks::ANY_REQUEST); });
#endif

                if (!active_ops.is_any(backend::op_bitmasks::ANY_REQUEST))
                {
                    // do nothing
                }
                else if (active_ops.is_requested(backend::op_category_e::DESTROY))
                {
                    active_ops.accept_request(backend::op_category_e::DESTROY);
                    m_open_status = status_e::NOT_OPEN;
                    destroyed     = true;
                }
                else
                {
                    if (active_ops.is_requested(backend::op_category_e::SEND))
                        active_ops.accept_request(backend::op_category_e::SEND);
                    if (active_ops.is_requested(backend::op_category_e::RECEIVE))
                        active_ops.accept_request(backend::op_category_e::RECEIVE);
                    if (active_ops.is_requested(backend::op_category_e::CLOSE))
                        active_ops.accept_request(backend::op_category_e::CLOSE);
                    if (active_ops.is_requested(backend::op_category_e::OPEN))
                    {
                        // save these new open settings, regardless of if they weill be successful later, to enable retries
                        if (p_open_opts != &m_open_opts)
                            set_open_args(*p_open_opts);
                        active_ops.accept_request(backend::op_category_e::OPEN);
                    }
                }
            }

#if CPPTXRX_THREADSAFE
            // notify caller that their transaction was accepted
            cv.notify_all();
#endif
            if (destroyed)
                return false;

            // do the transaction(s)
            {
#if CPPTXRX_THREADSAFE
                std::lock_guard<std::mutex> open_opts_lk(m_open_opts_mutex);
#endif
                // only do one operation at a time in order to quickly notify/wake the calling method
                // prioritizing closing --> then opening --> and then send/receiving
                if (transactions.p_close_op != nullptr)
                    process_close();
                else if (transactions.p_open_op != nullptr)
                    process_open();
                else if (transactions.p_send_op != nullptr || transactions.p_recv_op != nullptr)
                    process_send_receive();
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

            if (op_ptr->status == status_e::IN_PROGRESS)
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
                    return; // wait until the IN_PROGRESS operation is finished
            }

            // if not status_e::IN_PROGRESS, then end the transaction
            {
#if CPPTXRX_THREADSAFE
                std::lock_guard<std::mutex> lk(m);
#endif
                switch (op_ptr_type)
                {
                case backend::op_category_e::SEND:
                {
                    transactions.p_send_op = nullptr;
                    break;
                }
                case backend::op_category_e::RECEIVE:
                {
                    transactions.p_recv_op = nullptr;
                    break;
                }
                case backend::op_category_e::OPEN:
                {
                    m_open_status          = transactions.p_open_op->status;
                    transactions.p_open_op = nullptr;
                    p_open_opts            = nullptr;
                    break;
                }
                case backend::op_category_e::CLOSE:
                {
                    if (transactions.p_close_op->status == status_e::SUCCESS)
                        m_open_status = status_e::NOT_OPEN;
                    transactions.p_close_op = nullptr;
                    break;
                }
                case backend::op_category_e::CONSTRUCT:
                    // fallthrough
                case backend::op_category_e::DESTROY:
                    // fallthrough
                default:
                    break;
                }
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
                        if (active_ops.is_any(backend::op_bitmasks::ANY_DESTROY))
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

                // mark the operation request as started and populate any needed op data
                switch (op)
                {
                case backend::op_category_e::RECEIVE:
                {
                    if (m_open_status != status_e::SUCCESS)
                        return status_e::NOT_OPEN;
                    transactions.p_recv_op = reinterpret_cast<recv_op *>(op_src_data);
                    break;
                }
                case backend::op_category_e::SEND:
                {
                    if (m_open_status != status_e::SUCCESS)
                        return status_e::NOT_OPEN;
                    transactions.p_send_op = reinterpret_cast<send_op *>(op_src_data);
                    break;
                }
                case backend::op_category_e::CLOSE:
                {
                    if (m_open_status != status_e::SUCCESS)
                        return status_e::NOT_OPEN;
                    transactions.p_close_op = reinterpret_cast<close_op *>(op_src_data);
                    break;
                }
                case backend::op_category_e::OPEN:
                {
                    internal_open_op &op_src_data_ref = *reinterpret_cast<internal_open_op *>(op_src_data);
                    if (op_src_data_ref.open_behaviour == open_behaviour_e::FAIL_TO_OPEN_IF_ALREADY_OPEN && m_open_status == status_e::SUCCESS)
                        return status_e::FAILED_ALREADY_OPEN;

                    // if no options were specified, try and re-use the last options
                    if (op_src_data_ref.p_open_arguments == nullptr)
                    {
                        // if no options can be re-used, fail immediately with an error so that no nullptr p_open_opts can be passed in
                        {
#if CPPTXRX_THREADSAFE
                            std::lock_guard<std::mutex> open_opts_lk(m_open_opts_mutex);
#endif
                            if (!m_open_opts_initialized)
                                return status_e::NO_PRIOR_OPEN_ARGS;
                        }
                        p_open_opts = &m_open_opts;
                    }
                    else
                        p_open_opts = op_src_data_ref.p_open_arguments;
                    transactions.p_open_op = op_src_data_ref.p_open_op_data;
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
                wait_for_constructed(lk); // can't call wake_process before constructed

                // wake/notify the operations loop that there is a new operation request to process
                wake_process();
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
                            return active_ops.is_any(backend::op_bitmasks::ANY_DESTROY) ||
                                   active_ops.is_complete(op);
                        });
#endif
                if (active_ops.is_any(backend::op_bitmasks::ANY_DESTROY))
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