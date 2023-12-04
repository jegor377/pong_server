cmake_minimum_required(VERSION 3.27)
project(pong_server)
set(CMAKE_CXX_STANDARD 20)
add_executable(server server.cpp types.cpp)
