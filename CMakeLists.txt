cmake_minimum_required(VERSION 3.31)
project(PacketTransporter C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable warnings
if (MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Core sources
set(SOURCES
    application.c
    network.c
    network_test.c
    transport.c
    utils.c
    sender/sender.c
)

# The receiver directory has a leading space in its name
set(RECEIVER_SOURCE " receiver/receiver.c")

# Headers (for IDE visibility)
set(HEADERS
    application.h
    config.h
    debug.h
    network.h
    network_packets.h
    transport.h
    transport_packets.h
    transport_receiver.h
    transport_sender.h
    utils.h
)

add_executable(PacketTransporter ${SOURCES} ${RECEIVER_SOURCE} ${HEADERS})

target_include_directories(PacketTransporter PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(PacketTransporter ntdll)