cmake_minimum_required(VERSION 3.31)
project(PacketTransporter C)

set(CMAKE_C_STANDARD 11)

add_executable(PacketTransporter application.c network.c
        network.h
        transport.c
        debug.h
        utils.c
        utils.h
        network_packets.h
        transport_packets.h
        transport_sender.h
        transport_receiver.h)
