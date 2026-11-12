cmake_minimum_required(VERSION 3.19)
project(RadarSimulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)

qt_standard_project_setup()

enable_testing()

set(recording_ui_sources
    Recorder.cpp
    Recorder.h
    RecordingWriter_p.h
    RecordingReader.cpp
    RecordingReader.h
    ReplayController.cpp
    ReplayController.h
)

qt_add_executable(RadarSimulator
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    RadarFrame.h
    ${recording_ui_sources}
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

add_executable(radar_frame_test
    tests/radar_frame_test.cpp
    RadarServer.cpp
    RadarServer.h
    RadarSimulation.cpp
    RadarSimulation.h
)
target_link_libraries(radar_frame_test PRIVATE Qt::Core Qt::Network)
add_test(NAME radar_frame_test COMMAND radar_frame_test)

add_executable(radar_display_test
    tests/radar_display_test.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    RadarWidget.cpp
    RadarWidget.h
    RadarServer.cpp
    RadarServer.h
    RadarSimulation.cpp
    RadarSimulation.h
    ${recording_ui_sources}
)
target_link_libraries(radar_display_test PRIVATE Qt::Widgets Qt::Network)
add_test(NAME radar_display_test COMMAND radar_display_test)
set_tests_properties(radar_display_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(radar_recorder_test
    tests/radar_recorder_test.cpp
    Recorder.cpp
    Recorder.h
    RadarServer.cpp
    RadarServer.h
    RadarSimulation.cpp
    RadarSimulation.h
)
target_link_libraries(radar_recorder_test PRIVATE Qt::Core Qt::Network)
add_test(NAME radar_recorder_test COMMAND radar_recorder_test)

add_executable(radar_reader_test
    tests/radar_reader_test.cpp
    RecordingReader.cpp
    Recorder.cpp
    Recorder.h
    RadarServer.cpp
    RadarServer.h
    RadarSimulation.cpp
    RadarSimulation.h
)
target_link_libraries(radar_reader_test PRIVATE Qt::Core Qt::Network)
add_test(NAME radar_reader_test COMMAND radar_reader_test)

add_executable(radar_replay_test
    tests/radar_replay_test.cpp
    ReplayController.cpp
    ReplayController.h
    RecordingReader.cpp
)
target_link_libraries(radar_replay_test PRIVATE Qt::Core)
add_test(NAME radar_replay_test COMMAND radar_replay_test)

add_executable(radar_window_recording_test
    tests/radar_window_recording_test.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    RadarWidget.cpp
    RadarWidget.h
    RadarServer.cpp
    RadarServer.h
    RadarSimulation.cpp
    RadarSimulation.h
    ${recording_ui_sources}
)
target_link_libraries(radar_window_recording_test PRIVATE Qt::Widgets Qt::Network)
add_test(NAME radar_window_recording_test COMMAND radar_window_recording_test)
set_tests_properties(radar_window_recording_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
    radar_frame_test
    radar_display_test
    radar_recorder_test
    radar_reader_test
    radar_replay_test
    radar_window_recording_test
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
