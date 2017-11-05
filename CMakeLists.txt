cmake_minimum_required(VERSION 3.8)
project(homegear_philipshue)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        src/PhysicalInterfaces/HueBridge.cpp
        src/PhysicalInterfaces/HueBridge.h
        src/PhysicalInterfaces/IPhilipsHueInterface.cpp
        src/PhysicalInterfaces/IPhilipsHueInterface.h
        src/Factory.cpp
        src/Factory.h
        src/GD.cpp
        src/GD.h
        src/Interfaces.cpp
        src/Interfaces.h
        src/PacketManager.cpp
        src/PacketManager.h
        src/PhilipsHue.cpp
        src/PhilipsHue.h
        src/PhilipsHueCentral.cpp
        src/PhilipsHueCentral.h
        src/PhilipsHueDeviceTypes.h
        src/PhilipsHuePacket.cpp
        src/PhilipsHuePacket.h
        src/PhilipsHuePeer.cpp
        src/PhilipsHuePeer.h
        config.h)

add_library(homegear_philipshue ${SOURCE_FILES})