#include <algorithm>
#include <iostream>
#include <functional>
#include <limits>
#include <stdexcept>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "../RadarServer.h"
#include "../Recorder.h"
#include "../RecordingWriter_p.h"

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

QList<QJsonObject> readLines(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Open written recording");
    QList<QJsonObject> lines;
    while (!file.atEnd())
    {
        const auto line = file.readLine();
        const auto json = QJsonDocument::fromJson(line);
        require(line.endsWith('\n') && json.isObject(), "Each complete line is valid JSON");
        lines.append(json.object());
    }
    return lines;
}

void verifyComplete(const QList<QJsonObject>& lines)
{
    require(lines.size() >= 3, "Recording contains a header, initial frame and footer");
    const auto header = lines.first();
    require(header.value("type") == "header" &&
                header.value("format") == "RadarSimulatorRecording" &&
                header.value("version") == 1 && header.value("nominal_interval_ms") == 100 &&
                header.value("created_at_utc").toString().endsWith('Z'),
            "JSON Lines v1 header");
    qint64 previousTime = -1;
    for (qsizetype index = 1; index < lines.size() - 1; ++index)
    {
        const auto& frame = lines[index];
        const auto time = frame.value("elapsed_us").toInteger(-1);
        require(frame.value("type") == "frame" &&
                    frame.value("seq").toInteger(-1) == index - 1 &&
                    time > previousTime && (index != 1 || time == 0),
                "Session sequence and monotonic time");
        previousTime = time;
    }
    const auto end = lines.last();
    require(end.value("type") == "end" && end.value("frame_count").toInteger() == lines.size() - 2 &&
                end.value("duration_us").toInteger() >= previousTime, "Complete footer");
}

struct Outcome
{
    QString path;
    QString error;
    qint64 count = -1;
};

Outcome run(Recorder& recorder, const QString& path, int deadlineMs,
            const std::function<void()>& action)
{
    QEventLoop loop;
    Outcome outcome;
    QObject::connect(&recorder, &Recorder::saved, &loop, [&](const QString& saved, qint64 count) {
        outcome = {saved, {}, count};
        loop.quit();
    });
    QObject::connect(&recorder, &Recorder::failed, &loop,
                     [&](const QString& error, const QString& partial, qint64 count) {
        outcome = {partial, error, count};
        loop.quit();
    });
    QTimer::singleShot(deadlineMs, &loop, &QEventLoop::quit);
    require(recorder.start(path), "Recorder accepts a new session");
    action();
    loop.exec();
    require(outcome.count >= 0, "Recorder signaled completion within the deadline");
    return outcome;
}

void testFrameEncodingAndQueue()
{
    RadarFrame frame;
    const auto empty = QJsonDocument::fromJson(Recording::encodeFrame(frame)).object();
    require(empty.value("targets").isArray() && empty.value("targets").toArray().isEmpty() &&
                empty.value("tracks").isArray(), "Empty frame arrays");
    frame.targets.append(RadarTarget{42, 359.5, -2, -3, -0.5, TargetState::Detected});
    frame.tracks.append(RadarTrack{42, 359.5, -2, -3, -0.5, -5, 360.5, 12345, 2,
                                  TrackState::Lost, {{359.5, -2, 12345}}});
    frame.detectedTargetIds.append(42);
    frame.scan.direction = ScanDirection::Counterclockwise;
    const auto object = QJsonDocument::fromJson(Recording::encodeFrame(frame)).object();
    const auto target = object.value("targets").toArray().first().toObject();
    const auto track = object.value("tracks").toArray().first().toObject();
    require(target.value("distance_m") == -2 && target.value("state") == "detected" &&
                track.value("state") == "lost" && track.value("predicted_angle_deg") == 360.5 &&
                track.value("history").toArray().first().toObject().value("timestamp_utc_ms") == 12345 &&
                object.value("scan").toObject().value("direction") == "counterclockwise" &&
                object.value("detected_ids").toArray().first() == 42,
            "Single-target frame preserves units, track history and direction");
    frame.targets[0].angle = std::numeric_limits<double>::infinity();
    require(Recording::encodeFrame(frame).isEmpty(), "Reject nonfinite values");

    Recording::PendingFrames queue;
    for (size_t index = 0; index < Recording::MaxPendingFrames; ++index)
    {
        frame.sequence = index;
        require(queue.push(frame, 1), "Accept the first 100 frames");
    }
    require(!queue.push(frame, 1), "Reject frame 101");
    for (size_t index = 0; index < Recording::MaxPendingFrames; ++index)
        require(queue.pop().sequence == index, "FIFO sequence");
    require(queue.empty() && queue.push(frame, Recording::MaxPendingBytes) &&
                !queue.push(frame, 1), "Byte capacity is enforced independently");
    queue.pop();
    require(queue.push(frame, 1), "Popping releases byte capacity");
}

void testWriteFailures()
{
    class FailingDevice : public QIODevice
    {
    public:
        qint64 result = -1;

    protected:
        qint64 readData(char*, qint64) override { return -1; }
        qint64 writeData(const char*, qint64) override { return result; }
    };

    FailingDevice device;
    require(device.open(QIODevice::WriteOnly | QIODevice::Unbuffered), "Open failing test device");
    require(!Recording::writeCompleteLine(device, "{}\n"), "Reject an I/O error");
    device.result = 1;
    require(!Recording::writeCompleteLine(device, "{}\n"), "Reject a short write");
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    try
    {
        QTemporaryDir directory;
        require(directory.isValid(), "Create test directory");
        RadarServer server;
        Recorder recorder(server);
        const int soakMs = app.arguments().value(1).toInt();
        if (soakMs > 0)
        {
            int steps = 0;
            QObject::connect(&server, &RadarServer::simulationStepCompleted, &app, [&] { ++steps; });
            server.startScan();
            const auto output = run(recorder, directory.filePath("soak.radar.jsonl"), soakMs + 10000,
                                    [&] {
                QTimer::singleShot(soakMs, &recorder, [&] {
                    server.stopScan();
                    recorder.stop();
                });
            });
            if (!output.error.isEmpty() || output.count != steps + 2)
                std::cerr << "frames=" << output.count << " steps=" << steps
                          << " error=" << output.error.toStdString() << '\n';
            require(output.error.isEmpty() && output.count == steps + 2, "Soak saves all steps");
            const auto lines = readLines(output.path);
            verifyComplete(lines);
            QList<qint64> intervals;
            for (qsizetype index = 3; index <= steps + 1; ++index)
                intervals.append(lines[index].value("elapsed_us").toInteger() -
                                 lines[index - 1].value("elapsed_us").toInteger());
            std::sort(intervals.begin(), intervals.end());
            std::cout << "duration_us=" << lines.last().value("duration_us").toInteger()
                      << " frames=" << output.count << " steps=" << steps
                      << " bytes=" << QFileInfo(output.path).size()
                      << " step_p95_us=" << (intervals.isEmpty() ? 0 :
                                             intervals[(intervals.size() - 1) * 95 / 100])
                      << '\n';
            return 0;
        }

        testFrameEncodingAndQueue();
        testWriteFailures();
        const auto idlePath = directory.filePath("idle.radar.jsonl");
        auto output = run(recorder, idlePath, 5000, [&] { recorder.stop(); });
        require(output.error.isEmpty() && output.count == 2 &&
                    !QFileInfo::exists(idlePath + ".part"), "Save with no simulation steps");
        verifyComplete(readLines(idlePath));

        const auto livePath = directory.filePath("live.radar.jsonl");
        int steps = 0;
        auto connection = QObject::connect(&server, &RadarServer::simulationStepCompleted, &app, [&] {
            if (++steps == 1)
                server.setScanSpeed(10.0);
            if (steps == 2)
            {
                server.stopScan();
                recorder.stop();
            }
        });
        server.snapshot(); // The recording still starts at sequence zero.
        server.setScanSpeed(3590.0);
        server.setBeamWidth(360.0);
        output = run(recorder, livePath, 5000, [&] { server.startScan(); });
        QObject::disconnect(connection);
        require(output.error.isEmpty() && output.count == 4 && steps == 2,
                "One frame per step even with no UI, plus initial and final frames");
        const auto live = readLines(livePath);
        verifyComplete(live);
        require(live[2].value("scan").toObject().value("angle_deg") == 359 &&
                    live[3].value("scan").toObject().value("angle_deg") == 0 &&
                    live[2].value("targets").toArray().size() == 3 &&
                    live[2].value("tracks").toArray().size() == 3 &&
                    live[2].value("detected_ids").toArray().size() == 3 &&
                    live[3].value("targets").toArray().first().toObject().value("distance_m") == 1004,
                "Live stepping and scan wrap survive recording");

        const auto abortedPath = directory.filePath("aborted.radar.jsonl");
        output = run(recorder, abortedPath, 5000, [&] { recorder.abort(); });
        require(!output.error.isEmpty() && output.count == 1 && !QFileInfo::exists(abortedPath) &&
                    readLines(output.path).last().value("type") == "frame",
                "Abort leaves a complete prefix and no footer");
        output = run(recorder, idlePath, 5000, [] {});
        require(!output.error.isEmpty() && output.path.isEmpty(), "Refuse existing destination");
        verifyComplete(readLines(idlePath));
        output = run(recorder, directory.filePath("missing/file.jsonl"), 5000, [] {});
        require(!output.error.isEmpty() && output.count == 0, "Report invalid output directory");
        server.setBeamWidth(std::numeric_limits<double>::infinity());
        output = run(recorder, directory.filePath("bad-frame.jsonl"), 5000, [] {});
        require(!output.error.isEmpty() && output.count == 0 &&
                    readLines(output.path).size() == 1,
                "Writer failure leaves a valid header without a success footer");
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
