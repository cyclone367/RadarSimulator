#include <iostream>
#include <memory>
#include <stdexcept>

#include <QCoreApplication>
#include <QAbstractEventDispatcher>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QSemaphore>
#include <QThread>

#include "../ReplayController.h"

namespace
{
using State = ReplayController::State;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

// The real timer is only a watchdog. Playback deadlines are driven by a fake clock.
void until(const std::function<bool()>& done, const char* message)
{
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (done())
            loop.quit();
    });
    poll.start(1);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    if (!done())
        loop.exec();
    require(done(), message);
}

void createFile(const QString& path, int frames = 5)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "Create replay fixture");
    const auto write = [&](const QByteArray& line) {
        require(file.write(line) == line.size(), "Write replay fixture");
    };
    if (frames == 0)
        return;
    write("{\"type\":\"header\",\"format\":\"RadarSimulatorRecording\",\"version\":1,"
          "\"created_at_utc\":\"2026-11-04T08:00:00.000Z\",\"nominal_interval_ms\":100}\n");
    const qint64 times[] = {0, 37000, 145000, 312000, 440000};
    for (int index = 0; index < frames; ++index)
    {
        const auto time = frames == 5 ? times[index] : index * 100000LL;
        write("{\"type\":\"frame\",\"seq\":" + QByteArray::number(index) +
              ",\"elapsed_us\":" + QByteArray::number(time) +
              ",\"scan\":{\"running\":true,\"scanning\":true,\"angle_deg\":0,"
              "\"direction\":\"clockwise\",\"beam_width_deg\":20,\"max_range_m\":5000,"
              "\"speed_deg_s\":10},\"targets\":[],\"tracks\":[],\"detected_ids\":[]}\n");
    }
    const auto duration = frames == 5 ? 500000 : frames * 100000LL;
    write("{\"type\":\"end\",\"frame_count\":" + QByteArray::number(frames) +
          ",\"duration_us\":" + QByteArray::number(duration) + "}\n");
}

quint64 sequence(const ReplayController& replay)
{
    const auto frame = replay.currentFrame();
    return frame ? frame->sequence : 999999;
}

void drive(ReplayController& replay)
{
    require(QMetaObject::invokeMethod(&replay, "advance", Qt::DirectConnection),
            "Drive playback from the test clock");
}

void testPlayback(const QString& path)
{
    qint64 realUs = 1000;
    ReplayController replay(nullptr, [&] { return realUs; });
    bool controllerThread = true;
    QObject::connect(&replay, &ReplayController::frameChanged, &replay, [&](const RadarFrame&) {
        controllerThread = controllerThread && QThread::currentThread() == replay.thread();
    });
    replay.open(path);
    require(replay.isLoading() && replay.state() == State::Closed && !replay.currentFrame(),
            "Opening is asynchronous");
    until([&] { return replay.state() == State::Ready; }, "Open recording");
    require(sequence(replay) == 0 && replay.duration() == 500000, "Ready displays first frame");

    replay.play();
    replay.play();
    realUs += 36999;
    drive(replay);
    require(sequence(replay) == 0 && replay.currentTime() == 36999, "No early frame");
    ++realUs;
    drive(replay);
    until([&] { return sequence(replay) == 1; }, "Publish at the irregular 37 ms deadline");

    realUs += 10000;
    replay.pause();
    replay.pause();
    realUs += 5000000;
    require(replay.currentTime() == 47000 && sequence(replay) == 1, "Pause freezes playback time");
    require(replay.setSpeed(2) && !replay.setSpeed(3) && replay.speed() == 2, "Accept only valid rates");
    replay.resume();
    require(replay.currentTime() == 47000, "Resume has no jump");
    realUs += 49000;
    drive(replay);
    until([&] { return sequence(replay) == 2; }, "2x reaches frame 2");
    require(replay.currentTime() == 145000 && replay.currentFrame()->elapsedMicroseconds == 145000,
            "2x has exact logical time and preserves frame timestamp");
    require(replay.setSpeed(4) && replay.currentTime() == 145000, "Changing rate preserves position");
    realUs += 41750;
    drive(replay);
    until([&] { return sequence(replay) == 3; }, "4x reaches frame 3");
    require(replay.currentTime() == 312000, "4x has exact logical time");
    realUs += 32000;
    drive(replay);
    until([&] { return sequence(replay) == 4; }, "Display final frame");
    require(replay.state() == State::Playing, "Hold last frame during footer tail");
    realUs += 15000;
    drive(replay);
    require(replay.state() == State::Finished && replay.currentTime() == 500000 && controllerThread,
            "Finish at duration and deliver frames on the controller thread");

    replay.stop();
    replay.stop();
    until([&] { return replay.state() == State::Ready; }, "Stop rewinds to Ready");
    replay.stepForward();
    until([&] { return sequence(replay) == 1; }, "Step advances one frame");
    require(replay.state() == State::Paused && replay.currentTime() == 37000, "Step stays paused");

    QList<quint64> outputs;
    QObject::connect(&replay, &ReplayController::frameChanged, &replay,
                     [&](const RadarFrame& frame) { outputs.append(frame.sequence); });
    replay.seek(499999);
    replay.seek(0);
    replay.seek(150000);
    replay.play();
    until([&] { return sequence(replay) == 2 && replay.state() == State::Playing; },
          "Only latest seek applies and pending play resumes from its frame");
    require(outputs == QList<quint64>{2} && replay.currentTime() == 145000,
            "Stale seek responses are discarded and cursor is anchored to frame time");
    replay.close();
    require(replay.state() == State::Closed && !replay.currentFrame(), "Close resets display");
}

void testFailuresAndLifetime(const QString& path, const QString& empty, const QString& large)
{
    ReplayController replay;
    QString error;
    QObject::connect(&replay, &ReplayController::errorOccurred, &replay,
                     [&](const QString& message) { error = message; });
    replay.open(empty);
    until([&] { return replay.state() == State::Error; }, "Reject empty file");
    require(!error.isEmpty(), "Report empty file error");
    replay.open(path + ".missing");
    until([&] { return replay.state() == State::Error; }, "Report missing file");

    replay.open(large);
    replay.close();
    replay.open(path);
    until([&] { return replay.state() == State::Ready; }, "Reopen while old work is outstanding");
    require(replay.duration() == 500000 && sequence(replay) == 0, "Only newest file is displayed");
    QFile changed(path);
    require(changed.open(QIODevice::Append) && changed.write(" ") == 1, "Change recording on disk");
    changed.close();
    replay.seek(145000);
    until([&] { return replay.state() == State::Error; }, "Read error after external change");
    require(error.contains("changed"), "Propagate Reader modification error");

    auto closing = std::make_unique<ReplayController>();
    closing->open(large);
    auto* thread = closing->findChild<QThread*>();
    require(thread != nullptr, "Find owned reader thread");
    bool exited = false;
    QObject::connect(thread, &QThread::finished, QCoreApplication::instance(),
                     [&] { exited = true; }, Qt::QueuedConnection);
    closing.reset();
    until([&] { return exited; }, "Reader thread exits after controller destruction");
}

void testResponsiveness(const QString& path)
{
    qint64 realUs = 0;
    ReplayController replay(nullptr, [&] { return realUs; });
    replay.open(path);
    until([&] { return replay.state() == State::Ready; }, "Start the lazy reader");
    replay.close();
    auto* thread = replay.findChild<QThread*>();
    require(thread != nullptr, "Find reader for responsiveness test");
    until([&] { return thread->eventDispatcher() != nullptr; }, "Reader dispatcher starts");

    struct Gate
    {
        QSemaphore entered;
        QSemaphore release;
    };
    const auto gate = std::make_shared<Gate>();
    // Always release the worker even when an assertion fails.
    struct Unblock
    {
        std::shared_ptr<Gate> gate;
        ~Unblock() { gate->release.release(); }
    } unblock{gate};
    require(QMetaObject::invokeMethod(thread->eventDispatcher(), [gate] {
        gate->entered.release();
        gate->release.acquire();
    }, Qt::QueuedConnection), "Queue a deterministic reader stall");
    until([&] { return gate->entered.available() > 0; },
          "Reader enters test stall");

    replay.open(path);
    bool heartbeat = false;
    QTimer::singleShot(0, &replay, [&] { heartbeat = true; });
    until([&] { return heartbeat; }, "Controller events run while open is blocked");
    require(replay.isLoading() && replay.state() == State::Closed,
            "No load result before the worker is released");
    gate->release.release();
    until([&] { return replay.state() == State::Ready; }, "Load resumes after stall");

    const auto playbackGate = std::make_shared<Gate>();
    Unblock unblockPlayback{playbackGate};
    require(QMetaObject::invokeMethod(thread->eventDispatcher(), [playbackGate] {
        playbackGate->entered.release();
        playbackGate->release.acquire();
    }, Qt::QueuedConnection), "Queue reader stall during playback");
    until([&] { return playbackGate->entered.available() > 0; }, "Playback reader is stalled");
    replay.play();
    realUs += 2000000;
    drive(replay);
    heartbeat = false;
    QTimer::singleShot(0, &replay, [&] { heartbeat = true; });
    until([&] { return heartbeat; }, "Controller events run during playback with blocked reader");
    require(replay.state() == State::Playing && replay.currentTime() == 2000000,
            "Playback time advances independently of reader latency");
    replay.seek(245000);
    playbackGate->release.release();
    until([&] { return replay.state() == State::Paused && sequence(replay) == 2; },
          "Pending seek completes after stalled read");
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        QTemporaryDir dir;
        require(dir.isValid(), "Create test directory");
        const auto path = dir.filePath("sample.jsonl");
        const auto empty = dir.filePath("empty.jsonl");
        const auto large = dir.filePath("long.jsonl");
        createFile(path);
        createFile(empty, 0);
        createFile(large, 10000);
        testPlayback(path);
        testResponsiveness(large);
        testFailuresAndLifetime(path, empty, large);
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
