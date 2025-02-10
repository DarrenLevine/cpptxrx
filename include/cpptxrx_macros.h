/// @file cpptxrx_macros.h
/// @author Darren V Levine (DarrenVLevine@gmail.com)
/// @brief defines some helpful pre-compilation macros that can be used to set up a wrapper interface
///
/// The CPPTXRX_IMPORT_CTOR/DTOR/etc. macros help define the necessary constructors and destructors
/// unable to be inherited virtually, as well as ensuring the proper setup and teardown, and a consistent
/// constructor argument API. **They must not be neglected.**
///
/// Note that you can manually define the following patterns instead, though it is not recommended, since
/// future versions of this library might modify the internals of the macros, making it harder to upgrade:
///
///      class_name()
///      {
///          if constexpr (!threadsafe)
///              construct();
///      }
///      template <typename... TArgs>
///      class_name(TArgs &&...vargs) : class_name()
///      {
///          open(std::forward<TArgs>(vargs)...);
///      }
///      ~class_name()
///      {
///          destroy();
///      }
/// @copyright (c) 2024 Darren V Levine. This code is licensed under MIT license (see LICENSE file for details).
///
#ifndef CPPTXRX_MACROS_H_
#define CPPTXRX_MACROS_H_

/// @brief import the constructor pattern for a new cpptxrx interface
#define CPPTXRX_IMPORT_CTOR(class_name)              \
    class_name()                                     \
    {                                                \
        if constexpr (!threadsafe)                   \
            construct();                             \
    }                                                \
    class_name(const opts &open_opts) : class_name() \
    {                                                \
        open(open_opts);                             \
    }                                                \
    template <typename... TArgs>                     \
    class_name(TArgs &&...vargs) : class_name()      \
    {                                                \
        open(std::forward<TArgs>(vargs)...);         \
    }                                                \
    static_assert(1)

/// @brief import the destructor pattern for a new cpptxrx interface
#define CPPTXRX_IMPORT_DTOR(class_name) \
    ~class_name() final                 \
    {                                   \
        destroy();                      \
    }                                   \
    static_assert(1)

/// @brief import the constructor and destructor pattern for a new cpptxrx interface
#define CPPTXRX_IMPORT_CTOR_AND_DTOR(class_name) \
    CPPTXRX_IMPORT_CTOR(class_name);             \
    CPPTXRX_IMPORT_DTOR(class_name)

/// @brief import the constructor, and extend the destructor for a new cpptxrx interface
#define CPPTXRX_IMPORT_CTOR_EXTEND_DTOR(class_name, ...) \
    CPPTXRX_IMPORT_CTOR(class_name);                     \
    ~class_name() final                                  \
    {                                                    \
        destroy();                                       \
        __VA_ARGS__                                      \
    }                                                    \
    static_assert(1)

/// @brief applies the "named parameter idiom" pattern to create a setter method that supports method chaining
/// see this link for an explanation: https://isocpp.org/wiki/faq/ctors#named-parameter-idiom
/// you can use this CPPTXRX_OPTS_SETTER macro to apply the pattern for you, if your member variable is the
/// same name as your setter method, but with a "m_" prefix.
#define CPPTXRX_OPTS_SETTER(name)                             \
    inline constexpr opts &name(decltype(m_##name) new_value) \
    {                                                         \
        m_##name = new_value;                                 \
        return *this;                                         \
    }                                                         \
    static_assert(1)

/// @brief creates all the boilerplate for a demonstration/fake socket, useful for examples/demos
#define CPPTXRX_CREATE_EMPTY_DEMO_METHODS(sock_name)                    \
public:                                                                 \
    CPPTXRX_IMPORT_CTOR_AND_DTOR(sock_name);                            \
    [[nodiscard]] const char *name() const final { return #sock_name; } \
                                                                        \
private:                                                                \
    void construct() final {}                                           \
    void destruct() final {}                                            \
    void process_close() final { transactions.p_close_op->end_op(); }   \
    void process_open() final { transactions.p_open_op->end_op(); }     \
    void wake_process() final {}                                        \
    void process_send_receive() final                                   \
    {                                                                   \
        if (transactions.p_send_op)                                     \
            process_send(*transactions.p_send_op);                      \
        if (transactions.p_recv_op)                                     \
            process_recv(*transactions.p_recv_op);                      \
    }                                                                   \
    static_assert(1)

#endif // CPPTXRX_MACROS_H_
