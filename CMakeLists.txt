cmake_minimum_required(VERSION 3.14)

project(
    cpptxrx
    VERSION 1.1.0
    LANGUAGES CXX
    DESCRIPTION "C++ Transmit/Receive Interface Wrapper"
)

add_library(cpptxrx INTERFACE)

target_include_directories(cpptxrx INTERFACE include/
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# To include cpptxrx in your project, just add the following two lines to your CMakeLists.txt file
#   add_subdirectory(cpptxrx/)
#   target_link_libraries(your_project_name cpptxrx)
