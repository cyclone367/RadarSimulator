#pragma once

#include <memory>

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class QThread;
class RadarServer;

// Use on the same thread as the server. The server must outlive the recorder.
class Recorder : public QObject
{
    Q_OBJECT

public:
    explicit Recorder(RadarServer& server, QObject* parent = nullptr);
    ~Recorder() override;

    // True means the request was accepted. Path and I/O errors arrive via failed().
    bool start(const QString& path);
    void stop();
    void abort();

signals:
    void saved(const QString& path, qint64 frameCount);
    void failed(const QString& reason, const QString& partialPath, qint64 frameCount);

private:
    struct Session;
    void capture();
    void finish();
    static void writeSession(Session& session);

    RadarServer& server;
    QTimer idleTimer;
    QElapsedTimer clock;
    qint64 nextSequence = 0;
    qint64 lastElapsedUs = 0;
    std::unique_ptr<Session> session;
    QThread* writer = nullptr;
};
