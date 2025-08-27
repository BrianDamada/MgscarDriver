cmake_minimum_required(VERSION 3.10)

project(driverMgscar)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    include
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

# Linkagem para Windows (cross ou nativo)
if (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Link estático do runtime do GCC/STD e pthreads
    target_link_options(driverMgscar PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(driverMgscar PRIVATE -lwinpthread)

    # Necessário para SetupDi* e, dependendo do código, CM_*:
    target_link_libraries(driverMgscar PRIVATE setupapi cfgmgr32)
endif()