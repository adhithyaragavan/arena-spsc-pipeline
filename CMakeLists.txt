cmake_minimum_required(VERSION 3.20)
project(allocators CXX)                  # project name, C++ only

set(CMAKE_CXX_STANDARD 20)               # use C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)      # error if the compiler can't
set(CMAKE_CXX_EXTENSIONS OFF)            # -std=c++20, not gnu++20

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)          # Release gives -O3, so benchmarks are meaningful
endif()

find_package(Threads REQUIRED)           # gives -pthread (ne

# one executable, built from one source file
add_executable(main tests/main.cpp)
target_include_directories(main PRIVATE include)     # same a
target_compile_options(main PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(main PRIVATE Threads::Threads)