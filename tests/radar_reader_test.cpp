#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QTemporaryDir>

#include "../RadarServer.h"
#include "../Recorder.h"
#include "../RecordingReader.h"
#include "../RecordingWriter_p.h"

namespace
{
constexpr auto Header =
    R"({"type":"header","format":"RadarSimulatorRecording","version":1,"created_at_utc":"2026-11-04T08:00:00.000Z","nominal_interval_ms":100})";

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void write(QFile& file, const QByteArray& bytes)
{
    require(file.write(bytes) == bytes.size(), "Write fixture");
}

RadarFrame sample()
{
    RadarFrame frame;
    frame.scan = {true, true, 359, ScanDirection::Clockwise, 20, 5000, 10};
    frame.targets.append(RadarTarget{42, 359.5, -2, -3, -0.5, TargetState::Detected});
    frame.tracks.append(RadarTrack{42, 359.5, -2, -3, -0.5, -5, 360.5, 12345, 2,
                                  TrackState::Lost, {{359.5, -2, 12345}}});
    frame.detectedTargetIds.append(42);
    return frame;
}

QByteArray endRecord(int count)
{
    return "{\"type\":\"end\",\"frame_count\":" + QByteArray::number(count) +
           ",\"duration_us\":" + QByteArray::number(count * 100000LL) + "}\n";
}

void writeFixture(const QString& path, int count, bool benchmark)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "Create indexed fixture");
    write(file, QByteArray(Header) + "\n");
    auto frame = sample();
    if (benchmark)
    {
        const auto point = frame.tracks[0].history.first();
        while (frame.tracks[0].history.size() < 20)
            frame.tracks[0].history.append(point);
        for (int id : {43, 44})
        {
            auto target = frame.targets.first();
            auto track = frame.tracks.first();
            target.id = track.id = id;
            frame.targets.append(target);
            frame.tracks.append(track);
        }
    }
    for (int index = 0; index < count; ++index)
    {
        frame.sequence = index;
        frame.elapsedMicroseconds = index * 100000LL;
        frame.scan.angleDegrees = (359 + index) % 360;
        write(file, Recording::encodeFrame(frame));
    }
    write(file, endRecord(count));
    require(file.flush(), "Flush fixture");
}

void checkIndex(const QString& path, int count, bool benchmark)
{
    writeFixture(path, count, benchmark);
    RecordingReader reader;
    QElapsedTimer clock;
    clock.start();
    require(reader.open(path), "Open indexed fixture");
    const auto openMs = clock.elapsed();
    require(reader.indexEntryCount() == (count + 99) / 100 &&
                reader.frameCount() == count &&
                reader.durationMicroseconds() == count * 100000LL,
            "Index stores one entry per 100 frames and reads footer");
    const auto first = reader.frameAtSequence(0);
    const auto wrapped = reader.frameAtSequence(1);
    require(first && wrapped && first->scan.angleDegrees == 359 &&
                wrapped->scan.angleDegrees == 0,
            "Read scan across the wrap");
    const auto frame = reader.frameAtSequence(100);
    require(frame && frame->scan.direction == ScanDirection::Clockwise &&
                frame->scan.speedDegreesPerSecond == 10 &&
                frame->scan.maxRangeMeters == 5000 && frame->scan.beamWidthDegrees == 20 &&
                frame->scan.running && frame->scan.scanning &&
                frame->targets.first().id == 42 && frame->targets.first().distance == -2 &&
                frame->targets.first().angularSpeed == -0.5 &&
                frame->targets.first().state == TargetState::Detected &&
                frame->tracks.first().state == TrackState::Lost &&
                frame->tracks.first().estimatedSpeed == -3 &&
                frame->tracks.first().predictedAngle == 360.5 &&
                frame->tracks.first().hitCount == 2 &&
                frame->tracks.first().lastUpdateTime == 12345 &&
                frame->tracks.first().history.first().timestamp == 12345 &&
                frame->detectedTargetIds == QList<int>{42}, "Complete target, track and scan data");
    const auto last = reader.frameAtTime(count * 100000LL);
    require(!reader.frameAtSequence(count) && !reader.frameAtTime(-1) && last &&
                last->sequence == static_cast<quint64>(count - 1),
            "Lookup boundaries");

    std::mt19937 random(42);
    QList<qint64> latencies;
    for (int index = 0; index < (benchmark ? 1000 : 50); ++index)
    {
        const auto sequence = static_cast<quint64>(random() % count);
        clock.restart();
        const auto found = reader.frameAtTime(sequence * 100000 + 99999);
        latencies.append(clock.nsecsElapsed() / 1000);
        require(found && found->sequence == sequence, "Random time lookup returns floor frame");
    }
    if (benchmark)
    {
        std::sort(latencies.begin(), latencies.end());
        std::cout << "frames=" << count << " bytes=" << QFileInfo(path).size()
                  << " index_entries=" << reader.indexEntryCount()
                  << " index_capacity_bytes=" << reader.indexMemoryBytes()
                  << " open_ms=" << openMs << " seek_p95_us=" << latencies[949]
                  << " seek_p99_us=" << latencies[989] << '\n';
    }

    QFile modified(path);
    require(modified.open(QIODevice::Append), "Modify open recording externally");
    write(modified, " ");
    modified.close();
    require(!reader.frameAtSequence(0) && !reader.errorString().isEmpty() &&
                reader.frameCount() == 0, "External modification invalidates the index");
}

void checkInvalid(const QString& path)
{
    const auto header = QByteArray(Header) + "\n";
    const auto good = Recording::encodeFrame(sample());
    auto second = sample();
    second.sequence = 1;
    second.elapsedMicroseconds = 100000;
    const auto validSecond = Recording::encodeFrame(second);
    auto duplicate = validSecond;
    duplicate.replace("\"seq\":1", "\"seq\":0");
    auto skipped = validSecond;
    skipped.replace("\"seq\":1", "\"seq\":2");
    auto equalTime = validSecond;
    equalTime.replace("\"elapsed_us\":100000", "\"elapsed_us\":0");
    auto badField = validSecond;
    badField.replace("\"running\":true", "\"running\":1");
    auto missing = validSecond;
    missing.replace("\"detected_ids\":[42],", "");
    auto badDirection = validSecond;
    badDirection.replace("clockwise", "unknown");
    auto unknown = header;
    unknown.replace("\"version\":1", "\"version\":2");
    RecordingReader reader;
    const auto replace = [&](const QByteArray& bytes) {
        QFile file(path);
        require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Create damaged fixture");
        write(file, bytes);
    };

    for (const auto& bytes : QList<QByteArray>{QByteArray(), "{}\n" + good, unknown + good})
    {
        replace(bytes);
        require(!reader.open(path, RecordingReader::Mode::RecoverPrefix), "Reject unusable header");
    }
    require(reader.errorString().contains("Unsupported"), "Unknown version diagnostic");
    for (const auto& bad : QList<QByteArray>{
             QByteArray(), "{bad}\n", "{\"type\":", duplicate, skipped, equalTime,
             badField, missing, badDirection, endRecord(2), endRecord(1) + "trailing\n"})
    {
        replace(header + good + bad);
        require(!reader.open(path), "Strict mode rejects malformed or incomplete recording");
        require(reader.open(path, RecordingReader::Mode::RecoverPrefix) && reader.isRecovered() &&
                    reader.frameCount() == 1 && reader.durationMicroseconds() == 0 &&
                    !reader.recoveryReason().isEmpty() && reader.frameAtSequence(0) &&
                    !reader.frameAtSequence(1), "Recover only the first valid frame");
        reader.close();
    }
    replace(header + good + "{bad}\n" + validSecond + endRecord(2));
    require(reader.open(path, RecordingReader::Mode::RecoverPrefix) && reader.frameCount() == 1,
            "Recovery does not cross an invalid line");
    reader.close();
    auto crlf = header + good + endRecord(1);
    crlf.replace("\n", "\r\n");
    replace(crlf);
    require(reader.open(path) && !reader.isRecovered(), "Accept CRLF records");
}

void checkRecorderOutput(const QString& path)
{
    RadarServer server;
    Recorder recorder(server);
    QEventLoop loop;
    bool saved = false;
    QObject::connect(&recorder, &Recorder::saved, &loop, [&] { saved = true; loop.quit(); });
    QObject::connect(&recorder, &Recorder::failed, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    require(recorder.start(path), "Start real recorder");
    recorder.stop();
    loop.exec();
    RecordingReader reader;
    require(saved && reader.open(path) && reader.frameCount() == 2,
            "Open unmodified Recorder output");
    const auto frame = reader.frameAtSequence(0);
    require(frame && frame->targets.size() == 3, "Read actual Recorder targets");
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    try
    {
        QTemporaryDir directory;
        require(directory.isValid(), "Create reader test directory");
        if (app.arguments().contains("--benchmark"))
            checkIndex(directory.filePath("hour.jsonl"), 36000, true);
        else
        {
            checkRecorderOutput(directory.filePath("recorded.radar.jsonl"));
            checkInvalid(directory.filePath("damaged.radar.jsonl.part"));
            checkIndex(directory.filePath("indexed.radar.jsonl"), 251, false);
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
