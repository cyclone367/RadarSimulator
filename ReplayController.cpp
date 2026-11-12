#include "ReplayController.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>

#include <QThread>

namespace
{
constexpr int BufferCapacity = 8;
constexpr int RefillAt = 4;
}

struct ReplayController::Result
{
    Request request;
    QList<RadarFrame> frames;
    qint64 count = 0;
    qint64 duration = 0;
    QString recovery;
    QString error;
};

class ReplayReadWorker : public QObject
{
    Q_OBJECT

public:
    std::atomic<quint64> wantedRevision{0};

    void process(const ReplayController::Request& request)
    {
        using Kind = ReplayController::Request::Kind;
        ReplayController::Result result{request};
        const auto obsolete = [&] {
            return wantedRevision.load(std::memory_order_relaxed) != request.revision ||
                   QThread::currentThread()->isInterruptionRequested();
        };
        if (!obsolete())
        {
            if (!reader)
                reader = std::make_unique<RecordingReader>();
            if (request.kind == Kind::Close)
                reader.reset();
            else if (request.kind == Kind::Open && !reader->open(request.path, request.mode))
                result.error = reader->errorString();
            else if (request.kind == Kind::Prefetch)
            {
                for (int offset = 0; offset < request.amount && !obsolete(); ++offset)
                {
                    auto frame = reader->frameAtSequence(request.value + offset);
                    if (!frame)
                    {
                        result.error = reader->errorString();
                        if (result.error.isEmpty())
                            result.error = QStringLiteral("Could not read prefetched frame");
                        break;
                    }
                    result.frames.append(std::move(*frame));
                }
            }
            else if (!obsolete())
            {
                auto frame = request.kind == Kind::Seek
                    ? reader->frameAtTime(request.value) : reader->frameAtSequence(request.value);
                if (frame)
                {
                    result.frames.append(std::move(*frame));
                    result.count = reader->frameCount();
                    result.duration = reader->durationMicroseconds();
                    result.recovery = reader->recoveryReason();
                }
                else
                {
                    result.error = reader->errorString();
                    if (result.error.isEmpty())
                        result.error = QStringLiteral("Could not read requested frame");
                }
            }
        }
        emit completed(std::move(result));
    }

signals:
    void completed(ReplayController::Result result);

private:
    std::unique_ptr<RecordingReader> reader;
};

ReplayController::ReplayController(QObject* parent, std::function<qint64()> clock)
    : QObject(parent), now(std::move(clock))
{
    monotonic.start();
    if (!now)
        now = [this] { return monotonic.nsecsElapsed() / 1000; };
    timer.setSingleShot(true);
    timer.setTimerType(Qt::PreciseTimer);
    connect(&timer, &QTimer::timeout, this, &ReplayController::advance);
}

void ReplayController::startWorker()
{
    worker = new ReplayReadWorker;
    auto* thread = new QThread(this);
    readerThread = thread;
    thread->setObjectName(QStringLiteral("Replay reader"));
    worker->moveToThread(thread);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this] {
        readerThread = nullptr;
        emit shutdownFinished();
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(worker, &ReplayReadWorker::completed, this, &ReplayController::receive,
            Qt::QueuedConnection);
    thread->start();
}

ReplayController::~ReplayController()
{
    // The worker owns its reader and may still be finishing a synchronous read.
    // Its lifetime is independent of the controller and no result can reach it after destruction.
    if (readerThread)
    {
        if (worker)
            disconnect(worker, nullptr, this, nullptr);
        readerThread->setParent(nullptr);
        readerThread->requestInterruption();
        readerThread->quit();
    }
}

void ReplayController::setState(State state)
{
    if (playbackState == state)
        return;
    playbackState = state;
    emit stateChanged(state);
}

void ReplayController::cancelPending()
{
    ++revision;
    if (worker)
        worker->wantedRevision.store(revision, std::memory_order_relaxed);
    pending.reset();
    buffered.clear();
    timer.stop();
    positioning = false;
    playAfterPosition = false;
}

void ReplayController::resetDisplay()
{
    cancelPending();
    displayed.reset();
    opening = false;
    count = 0;
    durationUs = 0;
    positionUs = 0;
    recovery.clear();
}

void ReplayController::open(const QString& path, RecordingReader::Mode mode)
{
    if (!worker)
    {
        if (readerThread)
            return;
        startWorker();
    }
    resetDisplay();
    opening = true;
    request({Request::Kind::Open, revision, path, mode});
    setState(State::Closed);
}

void ReplayController::close()
{
    resetDisplay();
    if (worker)
        request({Request::Kind::Close, revision});
    setState(State::Closed);
}

bool ReplayController::shutdown()
{
    if (!readerThread)
        return false;
    if (worker)
    {
        resetDisplay();
        disconnect(worker, nullptr, this, nullptr);
        worker = nullptr;
        inFlight = false;
        readerThread->requestInterruption();
        readerThread->quit();
        setState(State::Closed);
    }
    return true;
}

void ReplayController::request(Request work)
{
    if (inFlight)
    {
        pending = std::move(work);
        return;
    }
    inFlight = true;
    QMetaObject::invokeMethod(worker, [target = worker, work = std::move(work)] {
        target->process(work);
    }, Qt::QueuedConnection);
}

void ReplayController::receive(Result result)
{
    inFlight = false;
    if (pending)
    {
        auto next = std::move(*pending);
        pending.reset();
        request(std::move(next));
    }
    if (result.request.revision != revision)
        return;

    using Kind = Request::Kind;
    const auto kind = result.request.kind;
    if (!result.error.isEmpty())
    {
        fail(result.error);
        return;
    }
    if (kind == Kind::Close)
        return;
    if (kind == Kind::Prefetch)
    {
        for (auto& frame : result.frames)
            buffered.push_back(std::move(frame));
        advance();
        return;
    }

    if (kind == Kind::Open)
    {
        opening = false;
        count = result.count;
        durationUs = result.duration;
        recovery = result.recovery;
    }
    positioning = false;
    displayed = std::move(result.frames.first());
    positionUs = displayed->elapsedMicroseconds;
    const auto frame = *displayed;
    const auto completedRevision = revision;
    setState(kind == Kind::Open || kind == Kind::Rewind ? State::Ready : State::Paused);
    if (completedRevision != revision)
        return;
    emit frameChanged(frame);
    if (completedRevision != revision)
        return;
    if (playAfterPosition)
    {
        playAfterPosition = false;
        play();
    }
    else
        prefetch();
}

void ReplayController::fail(const QString& reason)
{
    resetDisplay();
    request({Request::Kind::Close, revision});
    const auto failedRevision = revision;
    setState(State::Error);
    if (failedRevision == revision)
        emit errorOccurred(reason);
}

qint64 ReplayController::currentTime() const
{
    if (playbackState != State::Playing)
        return positionUs;
    const auto elapsed = std::max<qint64>(0, now() - anchorUs);
    const auto remaining = durationUs - positionUs;
    if (elapsed > remaining / multiplier)
        return durationUs;
    return positionUs + elapsed * multiplier;
}

bool ReplayController::setSpeed(int value)
{
    if (value != 1 && value != 2 && value != 4)
        return false;
    if (value != multiplier)
    {
        positionUs = currentTime();
        anchorUs = now();
        multiplier = value;
        advance();
    }
    return true;
}

void ReplayController::play()
{
    if (positioning)
    {
        playAfterPosition = true;
        return;
    }
    if (playbackState == State::Finished)
    {
        reposition(Request::Kind::Rewind, 0);
        if (positioning)
            playAfterPosition = true;
        return;
    }
    if (playbackState != State::Ready && playbackState != State::Paused)
        return;
    anchorUs = now();
    setState(State::Playing);
    advance();
}

void ReplayController::pause()
{
    playAfterPosition = false;
    if (playbackState != State::Playing)
        return;
    positionUs = currentTime();
    timer.stop();
    setState(State::Paused);
}

void ReplayController::resume()
{
    if (playbackState == State::Paused)
        play();
}

void ReplayController::reposition(Request::Kind kind, qint64 value)
{
    cancelPending();
    positioning = true;
    Request work{kind, revision};
    work.value = value;
    request(std::move(work));
    setState(State::Paused);
}

void ReplayController::stop()
{
    if (!displayed)
        return;
    if (playbackState == State::Ready && !positioning)
        return;
    positionUs = currentTime();
    reposition(Request::Kind::Rewind, 0);
}

void ReplayController::seek(qint64 elapsedMicroseconds)
{
    if (!displayed)
        return;
    const auto time = std::clamp<qint64>(elapsedMicroseconds, 0, durationUs);
    positionUs = currentTime();
    reposition(Request::Kind::Seek, time);
}

void ReplayController::stepForward()
{
    if (!displayed || positioning ||
        (playbackState != State::Ready && playbackState != State::Paused))
        return;
    const auto next = displayed->sequence + 1;
    if (next < static_cast<quint64>(count))
        reposition(Request::Kind::Step, static_cast<qint64>(next));
}

void ReplayController::prefetch()
{
    if (inFlight || !displayed || positioning || playbackState == State::Finished ||
        buffered.size() > RefillAt)
        return;
    const auto next = (buffered.empty() ? displayed->sequence : buffered.back().sequence) + 1;
    const auto available = count - static_cast<qint64>(next);
    if (available <= 0)
        return;
    Request work{Request::Kind::Prefetch, revision};
    work.value = static_cast<qint64>(next);
    work.amount = static_cast<int>(std::min<qint64>(available, BufferCapacity - buffered.size()));
    request(std::move(work));
}

void ReplayController::advance()
{
    if (playbackState != State::Playing)
    {
        prefetch();
        return;
    }
    timer.stop();
    const auto currentRevision = revision;
    const auto time = currentTime();
    bool changed = false;
    while (!buffered.empty() && buffered.front().elapsedMicroseconds <= time)
    {
        displayed = std::move(buffered.front());
        buffered.pop_front();
        changed = true;
    }
    if (changed)
    {
        const auto frame = *displayed;
        emit frameChanged(frame);
        if (currentRevision != revision || playbackState != State::Playing)
            return;
    }
    if (time == durationUs && displayed->sequence + 1 == static_cast<quint64>(count))
    {
        positionUs = durationUs;
        timer.stop();
        setState(State::Finished);
        return;
    }

    prefetch();
    if (!buffered.empty() || displayed->sequence + 1 == static_cast<quint64>(count))
    {
        const auto due = buffered.empty() ? durationUs : buffered.front().elapsedMicroseconds;
        const auto delay = std::max<qint64>(0, due - currentTime());
        const auto divisor = 1000 * multiplier;
        timer.start(static_cast<int>(std::min<qint64>((delay + divisor - 1) / divisor,
                                                      std::numeric_limits<int>::max())));
    }
}

#include "ReplayController.moc"
