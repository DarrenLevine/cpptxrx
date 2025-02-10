/// @brief includes copies all the tests into one file that can be run all at once
// 1
#include "test_filters.cpp"
// 2
#include "test_overflows.cpp"
// 3
#include "test_all_examples_run.cpp"
// 4 (running test_using_udp last since it has long running stress tests)
#include "test_using_udp.cpp"