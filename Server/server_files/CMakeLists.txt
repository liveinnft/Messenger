cmake_minimum_required(VERSION 3.10)
project(MessengerServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(Server
    main.cpp
    server.cpp
    database.cpp
)

target_link_libraries(Server Threads::Threads)
