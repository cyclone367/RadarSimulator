cmake_minimum_required(VERSION 3.19)
project(RadarSimulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)

qt_standard_project_setup()

enable_testing()

qt_add_executable(RadarSimulator
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    RadarServer.cpp
    RadarServer.h
    RadarSimulation.cpp
    RadarSimulation.h
    RadarWidget.cpp
    RadarWidget.h
    shared/LineProtocol.hpp
    shared/LineBuffer.hpp
)

add_executable(protocol_test
    tests/protocol_test.cpp
)

add_executable(tcp_client_test
    tests/tcp_client_test.cpp
)

add_executable(radar_server_state_test
    tests/radar_server_state_test.cpp
    RadarServer.cpp
    RadarServer.h
    RadarSimulation.cpp
    RadarSimulation.h
    shared/LineProtocol.hpp
    shared/LineBuffer.hpp
)

add_test(
    NAME protocol_test
    COMMAND protocol_test
)

add_test(
    NAME radar_server_state_test
    COMMAND radar_server_state_test
)

target_link_libraries(tcp_client_test
    PRIVATE
        Qt::Core
        Qt::Network
)

target_link_libraries(radar_server_state_test
    PRIVATE
        Qt::Core
        Qt::Network
)

set_target_properties(
    protocol_test
    tcp_client_test
    radar_server_state_test
    PROPERTIES
        WIN32_EXECUTABLE FALSE
)

target_link_libraries(RadarSimulator
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
)

install(TARGETS RadarSimulator
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET RadarSimulator
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
