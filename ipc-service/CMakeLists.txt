cmake_minimum_required(VERSION 3.10)
project(fast_ipc)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(include)

# Fast IPC library (named ton_ipc for compatibility)
add_library(ton_ipc SHARED
    src/fast_ipc.cpp
)

target_compile_options(ton_ipc PRIVATE -Wall -Wextra -O3)
target_link_libraries(ton_ipc pthread)

# Test server
add_executable(test_server
    test/test_server.cpp
)

target_link_libraries(test_server ton_ipc pthread)

# Installation
install(TARGETS ton_ipc
    LIBRARY DESTINATION lib
)

install(FILES 
    include/fast_ipc.h
    include/fast_ipc_hooks.h
    DESTINATION include/ton_ipc
)