#include <functional>
#include <iostream>
#include <stdexcept>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QTableWidget>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtMath>

#include "../mainwindow.h"
#include "../RadarServer.h"
#include "../RadarWidget.h"
#include "../RecordingWriter_p.h"
#include "../ReplayController.h"

namespace
{
using State = ReplayController::State;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void until(const std::function<bool()>& done, const char* message)
{
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (done())
            loop.quit();
    });
    poll.start(5);
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    if (!done())
        loop.exec();
    require(done(), message);
}

QPushButton* button(MainWindow& window, const char* name)
{
    auto* result = window.findChild<QPushButton*>(name);
    require(result != nullptr, name);
    return result;
}

QString label(MainWindow& window, const char* name)
{
    auto* result = window.findChild<QLabel*>(name);
    require(result != nullptr, name);
    return result->text();
}

void clickTrack(MainWindow& window, const RadarTrack& track)
{
    auto* radar = window.findChild<RadarWidget*>();
    const auto radius = qMin(radar->width(), radar->height()) / 2 - 20;
    const auto distance = radius * track.distance / 5000.0;
    const auto angle = qDegreesToRadians(track.angle);
    const QPointF point(radar->width() / 2 + distance * qSin(angle),
                        radar->height() / 2 - distance * qCos(angle));
    QMouseEvent click(QEvent::MouseButtonPress, point, radar->mapToGlobal(point),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(radar, &click);
}

void screenshot(MainWindow& window, const QString& directory, const QString& name)
{
    if (!directory.isEmpty())
    {
        QApplication::processEvents();
        require(window.grab().save(QDir(directory).filePath(name)), "Save actual window screenshot");
    }
}

void testWindow(RadarServer& server, quint16 port, const QString& path, const QString& screenshots)
{
    MainWindow window(&server);
    window.resize(1440, 960);
    window.show();
    auto* replay = window.findChild<ReplayController*>();
    auto* table = window.findChild<QTableWidget*>("tableTracks");
    auto* slider = window.findChild<QSlider*>("timeline");
    auto* speed = window.findChild<QComboBox*>("speedBox");
    require(replay && table && slider && speed, "Find recording and replay controls");

    server.setBeamWidth(360);
    server.startScan();
    until([&] { return table->rowCount() == 3; }, "Live radar updates track table");
    require(window.startRecording(path) && label(window, "operationState") == "Live · Recording",
            "Start recording in live mode");
    require(!window.openRecording(path), "Opening is disabled during recording");
    screenshot(window, screenshots, "RadarSimulator-v0.4-Live-Recording.png");

    int steps = 0;
    const auto stepConnection = QObject::connect(&server, &RadarServer::simulationStepCompleted,
                                                  &window, [&] { ++steps; });
    until([&] { return steps >= 5; }, "Record live simulation steps");
    QObject::disconnect(stepConnection);
    button(window, "saveButton")->click();
    require(label(window, "operationState") == "Live · Saving…" &&
                !window.startRecording(path), "Saving is asynchronous and disables new recording");
    until([&] { return window.statusBar()->currentMessage().startsWith("Saved "); },
          "Display successful save");
    require(window.openRecording(path) && label(window, "operationState") == "Loading recording…",
            "Saved file starts asynchronous load");
    until([&] { return replay->state() == State::Ready; }, "Saved file can be reopened");
    require(!button(window, "btnStartScan")->isEnabled() && !button(window, "recordButton")->isEnabled(),
            "Replay disables local live controls");

    const auto history = replay->currentFrame();
    require(history && history->tracks.size() == 3, "Replay first frame contains tracks");
    clickTrack(window, history->tracks.first());
    require(table->item(0, 0)->text() == "T1" &&
                label(window, "lblInspectorTarget") == "Target: T1" &&
                label(window, "lblInspectorDetection") == "Detection: YES",
            "Replay PPI selection, table and inspector share the same frame");
    const auto azimuth = label(window, "lblCurrentAzimuth");

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, port);
    until([&] { return client.state() == QAbstractSocket::ConnectedState; }, "Connect TCP in Replay");
    require(client.write("STOP\r\n") == 6, "Send the original TCP command");
    until([&] { return client.canReadLine(); }, "Receive original TCP response");
    require(client.readLine() == "STATUS STOPPED\r\n" && !server.isRadarRunning() &&
                !server.isScanning(), "TCP changes the live radar during Replay");
    QEventLoop ticks;
    QTimer::singleShot(120, &ticks, &QEventLoop::quit);
    ticks.exec();
    require(label(window, "lblRadarState") == "Radar: RUNNING" &&
                label(window, "lblCurrentAzimuth") == azimuth &&
                label(window, "lblInspectorTarget") == "Target: T1",
            "Live refresh and TCP do not overwrite historical display");

    button(window, "stepButton")->click();
    until([&] { return replay->currentFrame()->sequence == 1; }, "Step through replay UI");
    speed->setCurrentIndex(2);
    require(replay->speed() == 4, "Change replay speed from the window");
    slider->setValue(slider->maximum());
    slider->setValue(0);
    until([&] { return !replay->isPositioning() && replay->currentFrame()->sequence == 0; },
          "Rapid timeline changes apply only the latest result");
    screenshot(window, screenshots, "RadarSimulator-v0.4-Replay.png");

    button(window, "resumeButton")->click();
    require(replay->state() == State::Playing, "Resume from a seek");
    button(window, "pauseButton")->click();
    require(replay->state() == State::Paused, "Pause via window");
    button(window, "resumeButton")->click();
    until([&] { return replay->state() == State::Finished; }, "Replay reaches the end");
    require(label(window, "operationState").contains("Finished"), "Display Finished state");
    button(window, "stopButton")->click();
    until([&] { return replay->state() == State::Ready; }, "Stop rewinds replay");
    button(window, "playButton")->click();
    require(replay->state() == State::Playing, "Play from Ready");
    button(window, "liveButton")->click();
    require(label(window, "operationState") == "Live" &&
                label(window, "lblRadarState") == "Radar: STOPPED" &&
                button(window, "btnStartScan")->isEnabled(), "Switch to latest live state immediately");

    require(window.openRecording(path + ".missing"), "Accept an asynchronous failed open");
    until([&] { return label(window, "operationState") == "Replay failed"; }, "Show load failure");
    require(label(window, "lblRadarState") == "Radar: STOPPED", "Failed load keeps live display");
    window.close();
    until([&] { return !window.isVisible(); }, "Window closes after reader shutdown");
}

void testClose(RadarServer& server, const QString& path, bool alreadySaving)
{
    MainWindow window(&server);
    window.show();
    require(window.startRecording(path), "Start recording before close");
    if (alreadySaving)
        button(window, "saveButton")->click();
    require(!window.close() && window.isVisible() &&
                label(window, "operationState") == "Live · Saving…",
            "Closing defers until asynchronous save completes");
    until([&] { return !window.isVisible(); }, "Close automatically after saved signal");
    RecordingReader reader;
    require(reader.open(path), "Deferred close preserves a complete file");
}

void testLargeLoad(RadarServer& server, const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "Create large fixture");
    file.write("{\"type\":\"header\",\"format\":\"RadarSimulatorRecording\",\"version\":1,"
               "\"created_at_utc\":\"2026-11-04T08:00:00.000Z\",\"nominal_interval_ms\":100}\n");
    auto frame = server.snapshot();
    for (int i = 0; i < 10000; ++i)
    {
        frame.sequence = i;
        frame.elapsedMicroseconds = i * 100000LL;
        const auto line = Recording::encodeFrame(frame);
        require(!line.isEmpty() && file.write(line) == line.size(), "Write large fixture frame");
    }
    file.write("{\"type\":\"end\",\"frame_count\":10000,\"duration_us\":1000000000}\n");
    require(file.flush(), "Flush large fixture");
    file.close();

    MainWindow window(&server);
    window.show();
    require(window.openRecording(path) && label(window, "operationState") == "Loading recording…",
            "Large file open returns while indexing");
    bool heartbeat = false;
    QTimer::singleShot(0, &window, [&] { heartbeat = true; });
    until([&] { return heartbeat; }, "Window event loop remains active during open");
    until([&] { return label(window, "operationState").contains("Ready"); },
          "Large file eventually becomes ready");
    require(window.openRecording(path), "Reopen large fixture before closing");
    require(!window.close(), "Close during indexing is deferred without blocking");
    until([&] { return !window.isVisible(); }, "Window closes after pending reader work exits");
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    try
    {
        QTemporaryDir dir;
        require(dir.isValid(), "Create temporary test directory");
        RadarServer server;
        quint16 port = 39200;
        while (port < 39300 && !server.start(port))
            ++port;
        require(port < 39300, "Start real TCP listener");
        testWindow(server, port, dir.filePath("saved.radar.jsonl"), app.arguments().value(1));
        testClose(server, dir.filePath("closing-recording.radar.jsonl"), false);
        testClose(server, dir.filePath("closing-saving.radar.jsonl"), true);
        testLargeLoad(server, dir.filePath("large.radar.jsonl"));
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
