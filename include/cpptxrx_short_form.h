#ifndef CPPTXRX_SHORT_FORM_H_
#define CPPTXRX_SHORT_FORM_H_

/// @brief ctr is the CppTxRx short-form namespace.
/// It places all the names that wouldn't collide into a single shorter "ctr" namespace for convenience.
/// Sacrificing some of the organizational logic in the long-form version.
namespace ctr
{
    using namespace interface;
    using namespace interface::filters;
    using data_t             = filter::data_t;
    using storage_abstract_t = filter::storage_abstract_t;
    using result_e           = filter::result_e;
    using restrict_inputs_e  = filter::restrict_inputs_e;
    using restrict_storage_e = filter::restrict_storage_e;
} // namespace ctr

#endif // CPPTXRX_SHORT_FORM_H_