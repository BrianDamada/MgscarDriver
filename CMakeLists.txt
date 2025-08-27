cmake_minimum_required(VERSION 3.10)

project(driverMgscar)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    src/libs/password
    src/libs/serial/include
    src/libs/serialCommunicator
)

file(GLOB SERIAL_SOURCES
    src/libs/serial/src/*.cc
    src/libs/serial/src/impl/*.cc
    src/libs/serial/src/impl/list_ports/*.cc
)

add_executable(
    driverMgscar
    main.cpp
    src/libs/password/password.cpp
    src/libs/serialCommunicator/serialCommunicator.cpp
    ${SERIAL_SOURCES}
)

# Linkagem extra no Windows
if (WIN32)
    target_link_libraries(driverMgscar setupapi)
endif()
