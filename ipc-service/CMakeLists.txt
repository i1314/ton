cmake_minimum_required(VERSION 3.16)
project(ton_ipc_service)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(BUILD_TESTS "Build tests" ON)
option(BUILD_STATIC "Build static library" OFF)

# Source files
set(IPC_SOURCES
    ipc_service.cpp
)

# Create library
if(BUILD_STATIC)
    add_library(ton_ipc STATIC ${IPC_SOURCES})
else()
    add_library(ton_ipc SHARED ${IPC_SOURCES})
endif()

target_include_directories(ton_ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Compiler flags
target_compile_options(ton_ipc PRIVATE
    -Wall -Wextra -Wpedantic
    -O3 -fPIC
)

# Link libraries
target_link_libraries(ton_ipc PRIVATE pthread)

# Tests
if(BUILD_TESTS)
    enable_testing()
    
    # Test server
    add_executable(test_server test/test_server.cpp)
    target_link_libraries(test_server ton_ipc pthread)
    
    # Integration test
    add_test(NAME integration_test 
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/run_integration_test.sh
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

# Install
install(TARGETS ton_ipc
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

install(FILES ipc_service.h
        DESTINATION include/ton_ipc)