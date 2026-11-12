#include "Recorder.h"

#include "RadarServer.h"
#include "RecordingWriter_p.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#ifdef Q_OS_WIN
#include <io.h>
#include <qt_windows.h>
#else
#include <unistd.h>
#endif

namespace
{
constexpr qint64 MaxExactJsonInteger = 9007199254740991LL;

qint64 estimatedBytes(const RadarFrame& frame)
{
    qint64 bytes = sizeof(RadarFrame) +
        frame.targets.capacity() * sizeof(RadarTarget) +
        frame.tracks.capacity() * sizeof(RadarTrack) +
        frame.detectedTargetIds.capacity() * sizeof(int);
    for (const auto& track : frame.tracks)
        bytes += track.history.capacity() * sizeof(TrackPoint);
    return bytes;
}

bool publishFile(const QString& partial, const QString& destination)
{
#ifdef Q_OS_WIN
    return MoveFileW(reinterpret_cast<LPCWSTR>(partial.utf16()),
                     reinterpret_cast<LPCWSTR>(destination.utf16())) != 0;
#else
    // link() publishes the completed inode atomically and refuses an existing name.
    if (::link(QFile::encodeName(partial).constData(),
               QFile::encodeName(destination).constData()) != 0)
        return false;
    QFile::remove(partial);
    return true;
#endif
}

bool syncFile(QFile& file)
{
    if (!file.flush())
        return false;
#ifdef Q_OS_WIN
    return _commit(file.handle()) == 0;
#else
    return ::fsync(file.handle()) == 0;
#endif
}

QByteArray jsonLine(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}
}

QByteArray Recording::encodeFrame(const RadarFrame& frame)
{
    bool valid = true;
    const auto number = [&](double value) {
        valid = valid && std::isfinite(value);
        return value;
    };
    const auto timestamp = [&](qint64 value) {
        valid = valid && value >= -MaxExactJsonInteger && value <= MaxExactJsonInteger;
        return value;
    };

    QJsonArray targets;
    for (const auto& target : frame.targets)
        targets.append(QJsonObject{
            {"id", target.id}, {"angle_deg", number(target.angle)},
            {"distance_m", number(target.distance)}, {"speed_m_s", number(target.speed)},
            {"angular_speed_deg_s", number(target.angularSpeed)},
            {"state", target.state == TargetState::Detected ? "detected" : "outside"}
        });

    QJsonArray tracks;
    for (const auto& track : frame.tracks)
    {
        QJsonArray history;
        for (const auto& point : track.history)
            history.append(QJsonObject{
                {"angle_deg", number(point.angle)}, {"distance_m", number(point.distance)},
                {"timestamp_utc_ms", timestamp(point.timestamp)}
            });
        tracks.append(QJsonObject{
            {"id", track.id}, {"angle_deg", number(track.angle)},
            {"distance_m", number(track.distance)},
            {"estimated_speed_m_s", number(track.estimatedSpeed)},
            {"estimated_angular_speed_deg_s", number(track.estimatedAngularSpeed)},
            {"predicted_distance_m", number(track.predictedDistance)},
            {"predicted_angle_deg", number(track.predictedAngle)},
            {"last_update_utc_ms", timestamp(track.lastUpdateTime)}, {"hit_count", track.hitCount},
            {"state", track.state == TrackState::Tracking ? "tracking" : "lost"},
            {"history", history}
        });
    }

    QJsonArray detectedIds;
    for (int id : frame.detectedTargetIds)
        detectedIds.append(id);

    const auto& scan = frame.scan;
    const QJsonObject scanState{
        {"running", scan.running}, {"scanning", scan.scanning},
        {"angle_deg", number(scan.angleDegrees)},
        {"direction", scan.direction == ScanDirection::Clockwise ? "clockwise" : "counterclockwise"},
        {"beam_width_deg", number(scan.beamWidthDegrees)},
        {"max_range_m", number(scan.maxRangeMeters)},
        {"speed_deg_s", number(scan.speedDegreesPerSecond)}
    };
    if (!valid || frame.sequence > MaxExactJsonInteger || frame.elapsedMicroseconds < 0 ||
        frame.elapsedMicroseconds > MaxExactJsonInteger)
        return {};

    return jsonLine({
        {"type", "frame"}, {"seq", static_cast<qint64>(frame.sequence)},
        {"elapsed_us", frame.elapsedMicroseconds}, {"scan", scanState},
        {"targets", targets}, {"tracks", tracks}, {"detected_ids", detectedIds}
    });
}

struct Recorder::Session
{
    enum class State { Recording, Saving, Failed };

    QString path;
    QString createdAtUtc;
    std::mutex mutex;
    std::condition_variable ready;
    Recording::PendingFrames pending;
    State state = State::Recording;
    qint64 durationUs = 0;
    QString error;

    // Written only by the worker and read after it finishes.
    QString partialPath;
    qint64 written = 0;
};

Recorder::Recorder(RadarServer& server, QObject* parent)
    : QObject(parent), server(server), idleTimer(this)
{
    Q_ASSERT(server.thread() == thread());
    idleTimer.setSingleShot(true);
    idleTimer.setTimerType(Qt::PreciseTimer);
    idleTimer.setInterval(Recording::SampleIntervalMs);
    connect(&server, &RadarServer::simulationStepCompleted, this, &Recorder::capture);
    connect(&idleTimer, &QTimer::timeout, this, [this] {
        if (!this->server.isScanning())
            capture();
        else
            idleTimer.start();
    });
}

Recorder::~Recorder()
{
    if (writer)
    {
        abort();
        writer->wait();
    }
}

bool Recorder::start(const QString& path)
{
    if (session || path.isEmpty())
        return false;

    session = std::make_unique<Session>();
    session->path = QFileInfo(path).absoluteFilePath();
    session->createdAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    nextSequence = 0;
    lastElapsedUs = 0;
    clock.start();
    capture();

    writer = QThread::create([state = session.get()] { writeSession(*state); });
    writer->setParent(this);
    connect(writer, &QThread::finished, this, &Recorder::finish);
    writer->start();
    return true;
}

void Recorder::capture()
{
    if (!session)
        return;

    // No file operation or serialization runs while this lock is held.
    {
        std::lock_guard lock(session->mutex);
        if (session->state != Session::State::Recording)
            return;
    }
    auto frame = server.snapshot();
    const qint64 bytes = estimatedBytes(frame);
    frame.sequence = nextSequence;
    frame.elapsedMicroseconds = nextSequence == 0
        ? 0 : std::max(clock.nsecsElapsed() / 1000, lastElapsedUs + 1);
    const qint64 elapsed = frame.elapsedMicroseconds;
    {
        std::lock_guard lock(session->mutex);
        if (session->state != Session::State::Recording)
            return;
        if (!session->pending.push(std::move(frame), bytes))
        {
            session->state = Session::State::Failed;
            session->error = QStringLiteral("Recording queue capacity exceeded");
            idleTimer.stop();
        }
        else
        {
            ++nextSequence;
            lastElapsedUs = elapsed;
            idleTimer.start();
        }
    }
    session->ready.notify_one();
}

void Recorder::stop()
{
    if (!session)
        return;

    // Capture the final state even when there were no simulation steps.
    capture();
    idleTimer.stop();
    {
        std::lock_guard lock(session->mutex);
        if (session->state != Session::State::Recording)
            return;
        session->durationUs = std::max(clock.nsecsElapsed() / 1000, lastElapsedUs);
        session->state = Session::State::Saving;
    }
    session->ready.notify_one();
}

void Recorder::abort()
{
    if (!session)
        return;
    idleTimer.stop();
    {
        std::lock_guard lock(session->mutex);
        if (session->state != Session::State::Recording)
            return;
        session->state = Session::State::Failed;
        session->error = QStringLiteral("Recording aborted");
    }
    session->ready.notify_one();
}

void Recorder::finish()
{
    idleTimer.stop();
    writer->deleteLater();
    writer = nullptr;
    const auto finished = std::move(session);
    if (finished->error.isEmpty())
        emit saved(finished->path, finished->written);
    else
        emit failed(finished->error, finished->partialPath, finished->written);
}

void Recorder::writeSession(Session& session)
{
    const auto fail = [&](const QString& reason) {
        std::lock_guard lock(session.mutex);
        session.state = Session::State::Failed;
        if (session.error.isEmpty())
            session.error = reason;
        else
            session.error += QStringLiteral("; ") + reason;
    };
    if (QFileInfo::exists(session.path))
    {
        fail(QStringLiteral("Recording destination already exists"));
        return;
    }

    QFile file(session.path + QStringLiteral(".part"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    {
        fail(file.errorString());
        return;
    }
    session.partialPath = file.fileName();
    const auto write = [&](const QByteArray& line) {
        if (Recording::writeCompleteLine(file, line))
            return true;
        fail(file.errorString());
        return false;
    };

    if (!write(jsonLine({
            {"type", "header"}, {"format", "RadarSimulatorRecording"}, {"version", 1},
            {"created_at_utc", session.createdAtUtc},
            {"nominal_interval_ms", Recording::SampleIntervalMs}
        })))
        return;

    QElapsedTimer flushClock;
    flushClock.start();
    for (;;)
    {
        std::unique_lock lock(session.mutex);
        session.ready.wait_for(lock, std::chrono::milliseconds(100), [&] {
            return !session.pending.empty() || session.state != Session::State::Recording;
        });
        if (session.pending.empty() && session.state != Session::State::Recording)
            break;
        const bool hasFrame = !session.pending.empty();
        RadarFrame frame;
        if (hasFrame)
            frame = session.pending.pop();
        lock.unlock();

        if (hasFrame)
        {
            const auto line = Recording::encodeFrame(frame);
            if (line.isEmpty() || line.size() - 1 > Recording::MaxLineBytes)
            {
                fail(QStringLiteral("Frame is not representable within the JSON Lines v1 limits"));
                return;
            }
            if (!write(line))
                return;
            ++session.written;
        }
        if (flushClock.elapsed() >= 1000)
        {
            if (!file.flush())
            {
                fail(file.errorString());
                return;
            }
            flushClock.restart();
        }
    }

    if (session.state == Session::State::Failed)
        return;

    const qint64 endOffset = file.pos();
    if (!write(jsonLine({
            {"type", "end"}, {"frame_count", session.written}, {"duration_us", session.durationUs}
        })) || !syncFile(file))
    {
        if (session.error.isEmpty())
            fail(QStringLiteral("Could not synchronize recording file"));
        file.resize(endOffset);
        return;
    }
    file.close();
    if (file.error() != QFileDevice::NoError)
    {
        fail(file.errorString());
        return;
    }
    if (!publishFile(session.partialPath, session.path))
        fail(QStringLiteral("Could not publish recording without replacing the destination"));
}
