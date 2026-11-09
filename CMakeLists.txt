cmake_minimum_required(VERSION 3.20)
project(UdpMarketDataFeeder VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# High Performance Compiler Flags
if(MSVC)
    add_compile_options(/O2 /Oi /Ot /GL /EHsc /W4)
else()
    add_compile_options(-O3 -march=native -Wall -Wextra -pthread)
endif()

add_executable(udp_feeder src/main.cpp)

if(WIN32)
    target_link_libraries(udp_feeder ws2_32)
endif()
