#pragma once

#include <deque>
#include <functional>
#include <optional>

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include "RecordingReader.h"

class ReplayReadWorker;
class QThread;

// All public operations and notifications belong to the controller's thread.
class ReplayController : public QObject
{
    Q_OBJECT

public:
    enum class State { Closed, Ready, Playing, Paused, Finished, Error };
    Q_ENUM(State)

    // An injected clock supplies monotonic microseconds, useful for deterministic tests.
    explicit ReplayController(QObject* parent = nullptr,
                              std::function<qint64()> clock = {});
    ~ReplayController() override;

    void open(const QString& path, RecordingReader::Mode mode = RecordingReader::Mode::Strict);
    void close();
    // Returns true if shutdownFinished will be emitted asynchronously.
    bool shutdown();
    void play();
    void pause();
    void resume();
    void stop();
    void stepForward();
    void seek(qint64 elapsedMicroseconds);
    bool setSpeed(int multiplier);

    State state() const { return playbackState; }
    int speed() const { return multiplier; }
    qint64 currentTime() const;
    qint64 duration() const { return durationUs; }
    std::optional<RadarFrame> currentFrame() const { return displayed; }
    bool isLoading() const { return opening; }
    bool isPositioning() const { return positioning; }
    QString recoveryReason() const { return recovery; }

signals:
    void stateChanged(ReplayController::State state);
    void frameChanged(const RadarFrame& frame);
    void errorOccurred(const QString& message);
    void shutdownFinished();

private slots:
    void advance();

private:
    friend class ReplayReadWorker;
    struct Request
    {
        enum class Kind { Open, Close, Seek, Step, Rewind, Prefetch };
        Kind kind;
        quint64 revision = 0;
        QString path;
        RecordingReader::Mode mode = RecordingReader::Mode::Strict;
        qint64 value = 0;
        int amount = 0;
    };
    struct Result;

    void setState(State state);
    void startWorker();
    void cancelPending();
    void request(Request work);
    void receive(Result result);
    void prefetch();
    void reposition(Request::Kind kind, qint64 value);
    void resetDisplay();
    void fail(const QString& reason);

    ReplayReadWorker* worker = nullptr;
    QThread* readerThread = nullptr;
    bool inFlight = false;
    std::optional<Request> pending;
    quint64 revision = 0;
    bool opening = false;
    bool positioning = false;
    bool playAfterPosition = false;
    State playbackState = State::Closed;
    int multiplier = 1;
    qint64 count = 0;
    qint64 durationUs = 0;
    qint64 positionUs = 0;
    qint64 anchorUs = 0;
    QString recovery;
    std::optional<RadarFrame> displayed;
    std::deque<RadarFrame> buffered;
    QElapsedTimer monotonic;
    std::function<qint64()> now;
    QTimer timer;
};
