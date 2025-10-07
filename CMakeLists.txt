cmake_minimum_required(VERSION 3.28)
project(parbfs CXX)
set(CMAKE_CXX_STANDARD 23)

add_executable(parbfs main.cpp bfs.cpp)
target_link_libraries(parbfs fmt)