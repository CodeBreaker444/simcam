cmake_minimum_required(VERSION 3.20)
project(IPCameraSimulator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Svg)

set(SOURCES
    main.cpp
    mainwindow.cpp
    networkutils.cpp
    mediamtxmanager.cpp
    rtspstreamer.cpp
    wsdiscovery.cpp
    onvifserver.cpp
    resources.qrc          # Qt resources (SVG icon, etc.)
)

set(HEADERS
    mainwindow.h
    networkutils.h
    cameraprofile.h
    mediamtxmanager.h
    rtspstreamer.h
    wsdiscovery.h
    onvifserver.h
)

# Include Windows resource file only when appicon.ico is present.
# To enable: convert appicon.svg → appicon.ico and place it next to app.rc.
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/appicon.ico")
    list(APPEND SOURCES app.rc)
endif()

if(WIN32)
    add_executable(IPCameraSimulator WIN32 ${SOURCES} ${HEADERS})
else()
    add_executable(IPCameraSimulator ${SOURCES} ${HEADERS})
endif()

target_link_libraries(IPCameraSimulator
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
)

if(WIN32)
    target_link_libraries(IPCameraSimulator ws2_32)
endif()

target_compile_definitions(IPCameraSimulator PRIVATE
    QT_NO_DEPRECATED_WARNINGS
    NOMINMAX
)

install(TARGETS IPCameraSimulator
    RUNTIME DESTINATION bin
)
