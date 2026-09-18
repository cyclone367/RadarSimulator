cmake_minimum_required(VERSION 3.19)
project(RadarSimulator LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)

qt_standard_project_setup()

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

target_link_libraries(tcp_client_test
    PRIVATE
        Qt::Core
        Qt::Network
)

set_target_properties(
    protocol_test
    tcp_client_test
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
