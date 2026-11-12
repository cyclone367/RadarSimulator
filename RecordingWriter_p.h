#pragma once

#include <deque>
#include <utility>

#include <QByteArray>
#include <QIODevice>

#include "RadarFrame.h"

namespace Recording
{
constexpr int SampleIntervalMs = 100;
constexpr qint64 MaxPendingBytes = 64 * 1024 * 1024;
constexpr size_t MaxPendingFrames = 100;
constexpr qsizetype MaxLineBytes = 8 * 1024 * 1024;

// Access under the session mutex. The in-flight frame is owned by the writer.
struct PendingFrames
{
    bool push(RadarFrame frame, qint64 bytes)
    {
        if (frames.size() >= MaxPendingFrames || bytes > MaxPendingBytes - pendingBytes)
            return false;
        frames.push_back({std::move(frame), bytes});
        pendingBytes += bytes;
        return true;
    }

    RadarFrame pop()
    {
        auto entry = std::move(frames.front());
        frames.pop_front();
        pendingBytes -= entry.bytes;
        return std::move(entry.frame);
    }

    bool empty() const { return frames.empty(); }

private:
    struct Entry { RadarFrame frame; qint64 bytes; };
    std::deque<Entry> frames;
    qint64 pendingBytes = 0;
};

// Empty means the frame cannot be represented in JSON Lines v1.
QByteArray encodeFrame(const RadarFrame& frame);

inline bool writeCompleteLine(QIODevice& device, const QByteArray& line)
{
    return device.write(line) == line.size();
}
}
