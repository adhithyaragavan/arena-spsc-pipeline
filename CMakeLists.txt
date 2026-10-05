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

add_executable(spsc_demo tests/spsc_demo.cpp)
target_include_directories(spsc_demo PRIVATE include)
target_compile_options(spsc_demo PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(spsc_demo PRIVATE Threads::Threads)

add_executable(pipeline bench/pipeline.cpp)
target_include_directories(pipeline PRIVATE include)
target_compile_options(pipeline PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(pipeline PRIVATE Threads::Threads)

add_executable(baseline bench/baseline.cpp)
target_include_directories(baseline PRIVATE include)
target_compile_options(baseline PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(baseline PRIVATE Threads::Threads)

add_executable(isolation bench/isolation.cpp)
target_include_directories(isolation PRIVATE include)
target_compile_options(isolation PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(isolation PRIVATE Threads::Threads)


enable_testing()

add_executable(test_arena tests/test_arena.cpp)
target_include_directories(test_arena PRIVATE include)
target_compile_options(test_arena PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME arena COMMAND test_arena)

add_executable(test_spsc tests/test_spsc.cpp)
target_include_directories(test_spsc PRIVATE include)
target_compile_options(test_spsc PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(test_spsc PRIVATE Threads::Threads)
add_test(NAME spsc COMMAND test_spsc)
