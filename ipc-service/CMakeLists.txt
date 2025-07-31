cmake_minimum_required(VERSION 3.10)
project(fast_ipc)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(include)

# Fast IPC library
add_library(fast_ipc SHARED
    src/fast_ipc.cpp
)

target_compile_options(fast_ipc PRIVATE -Wall -Wextra -O3)
target_link_libraries(fast_ipc pthread)

# Test server
add_executable(test_server
    test/test_server.cpp
)

target_link_libraries(test_server fast_ipc pthread)

# Installation
install(TARGETS fast_ipc
    LIBRARY DESTINATION lib
)

install(FILES 
    include/fast_ipc.h
    include/fast_ipc_hooks.h
    DESTINATION include/ton_ipc
)