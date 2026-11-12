#pragma once

#include <optional>
#include <vector>

#include <QDateTime>
#include <QFile>

#include "RadarFrame.h"

// Synchronous, read-only API. Keep each instance on one thread and run file
// operations on a worker when integrating playback with the UI.
class RecordingReader
{
public:
    enum class Mode { Strict, RecoverPrefix };

    bool open(const QString& path, Mode mode = Mode::Strict);
    void close();

    bool isRecovered() const { return recovered; }
    QString errorString() const { return error; }
    QString recoveryReason() const { return recovery; }
    qint64 frameCount() const { return count; }
    qint64 durationMicroseconds() const { return durationUs; }
    qsizetype indexEntryCount() const { return static_cast<qsizetype>(index.size()); }
    qsizetype indexMemoryBytes() const { return index.capacity() * sizeof(IndexEntry); }

    std::optional<RadarFrame> frameAtSequence(quint64 sequence);
    // The last frame at or before the requested time; times after EOF select the last frame.
    std::optional<RadarFrame> frameAtTime(qint64 elapsedMicroseconds);

private:
    struct IndexEntry
    {
        quint64 sequence;
        qint64 elapsedUs;
        qint64 offset;
    };

    bool unchanged() const;
    bool invalidate(const QString& reason);
    std::optional<RadarFrame> readSegment(size_t segment, quint64 sequence,
                                          std::optional<qint64> time);

    QFile file;
    qint64 originalSize = 0;
    QDateTime modified;
    std::vector<IndexEntry> index;
    qint64 count = 0;
    qint64 durationUs = 0;
    bool recovered = false;
    QString error;
    QString recovery;
};
