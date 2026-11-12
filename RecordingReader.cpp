#include "RecordingReader.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace
{
constexpr qint64 MaxExactInteger = 9007199254740991LL;
constexpr qint64 MaxLineBytes = 8 * 1024 * 1024;
constexpr size_t MaxIndexBytes = 32 * 1024 * 1024;
constexpr quint64 IndexStride = 100;

struct ParseError { QString message; };

void expect(bool condition, const QString& message)
{
    if (!condition)
        throw ParseError{message};
}

QJsonValue field(const QJsonObject& object, const char* name, QJsonValue::Type type)
{
    const auto value = object.value(QLatin1String(name));
    if (value.type() != type)
        throw ParseError{QStringLiteral("Missing or invalid field: %1").arg(QLatin1String(name))};
    return value;
}

double number(const QJsonObject& object, const char* name)
{
    const double value = field(object, name, QJsonValue::Double).toDouble();
    if (!std::isfinite(value))
        throw ParseError{QStringLiteral("Non-finite field: %1").arg(QLatin1String(name))};
    return value;
}

qint64 integer(const QJsonValue& value, const char* name,
               qint64 minimum = -MaxExactInteger, qint64 maximum = MaxExactInteger)
{
    const auto result = value.toInteger(std::numeric_limits<qint64>::min());
    if (result < minimum || result > maximum)
        throw ParseError{QStringLiteral("Invalid integer field: %1").arg(QLatin1String(name))};
    return result;
}

qint64 integer(const QJsonObject& object, const char* name, qint64 minimum = -MaxExactInteger,
               qint64 maximum = MaxExactInteger)
{
    return integer(object.value(QLatin1String(name)), name, minimum, maximum);
}

int modelInt(const QJsonValue& value, const char* name, int minimum = std::numeric_limits<int>::min())
{
    return static_cast<int>(integer(value, name, minimum, std::numeric_limits<int>::max()));
}

QJsonObject asObject(const QJsonValue& value, const char* name)
{
    if (!value.isObject())
        throw ParseError{QStringLiteral("Expected object in %1").arg(QLatin1String(name))};
    return value.toObject();
}

QJsonObject readRecord(QFile& file)
{
    auto line = file.readLine(MaxLineBytes + 3);
    expect(!line.isEmpty(), QStringLiteral("Missing record"));
    expect(line.endsWith('\n'), QStringLiteral("Unterminated or oversized JSON line"));
    line.chop(line.endsWith("\r\n") ? 2 : 1);
    expect(line.size() <= MaxLineBytes, QStringLiteral("JSON line exceeds 8 MiB"));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        throw ParseError{parseError.errorString()};
    expect(document.isObject(), QStringLiteral("Expected a JSON object"));
    return document.object();
}

void checkHeader(const QJsonObject& header)
{
    expect(header.value("type") == "header" &&
           header.value("format") == "RadarSimulatorRecording", QStringLiteral("Invalid recording header"));
    expect(integer(header, "version", 0) == 1, QStringLiteral("Unsupported recording version"));
    integer(header, "nominal_interval_ms", 1);
    const auto created = field(header, "created_at_utc", QJsonValue::String).toString();
    static const QRegularExpression utc(QStringLiteral(
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}(?:Z|\+00:00)$)"));
    expect(utc.match(created).hasMatch() && QDateTime::fromString(created, Qt::ISODateWithMs).isValid(),
           QStringLiteral("Invalid UTC creation timestamp"));
}

RadarFrame parseFrame(const QJsonObject& object)
{
    expect(object.value("type") == "frame", QStringLiteral("Expected frame record"));
    RadarFrame frame;
    frame.sequence = integer(object, "seq", 0);
    frame.elapsedMicroseconds = integer(object, "elapsed_us", 0);

    const auto scan = field(object, "scan", QJsonValue::Object).toObject();
    const auto direction = field(scan, "direction", QJsonValue::String).toString();
    expect(direction == "clockwise" || direction == "counterclockwise",
           QStringLiteral("Invalid scan direction"));
    frame.scan = {
        field(scan, "running", QJsonValue::Bool).toBool(),
        field(scan, "scanning", QJsonValue::Bool).toBool(),
        number(scan, "angle_deg"),
        direction == "clockwise" ? ScanDirection::Clockwise : ScanDirection::Counterclockwise,
        number(scan, "beam_width_deg"),
        number(scan, "max_range_m"),
        number(scan, "speed_deg_s")
    };
    expect(frame.scan.speedDegreesPerSecond >= 0 && frame.scan.beamWidthDegrees >= 0 &&
           frame.scan.maxRangeMeters >= 0, QStringLiteral("Negative scan magnitude"));

    QSet<int> targetIds;
    for (const auto& value : field(object, "targets", QJsonValue::Array).toArray())
    {
        const auto target = asObject(value, "targets");
        const int id = modelInt(target.value("id"), "target.id");
        expect(!targetIds.contains(id), QStringLiteral("Duplicate target ID"));
        targetIds.insert(id);
        const auto state = field(target, "state", QJsonValue::String).toString();
        expect(state == "outside" || state == "detected", QStringLiteral("Invalid target state"));
        frame.targets.append(RadarTarget{
            id, number(target, "angle_deg"), number(target, "distance_m"),
            number(target, "speed_m_s"), number(target, "angular_speed_deg_s"),
            state == "detected" ? TargetState::Detected : TargetState::Outside
        });
    }

    QSet<int> trackIds;
    for (const auto& value : field(object, "tracks", QJsonValue::Array).toArray())
    {
        const auto track = asObject(value, "tracks");
        const int id = modelInt(track.value("id"), "track.id");
        expect(!trackIds.contains(id), QStringLiteral("Duplicate track ID"));
        trackIds.insert(id);
        const auto state = field(track, "state", QJsonValue::String).toString();
        expect(state == "tracking" || state == "lost", QStringLiteral("Invalid track state"));
        RadarTrack data{
            id, number(track, "angle_deg"), number(track, "distance_m"),
            number(track, "estimated_speed_m_s"), number(track, "estimated_angular_speed_deg_s"),
            number(track, "predicted_distance_m"), number(track, "predicted_angle_deg"),
            integer(track, "last_update_utc_ms"), modelInt(track.value("hit_count"), "hit_count", 0),
            state == "tracking" ? TrackState::Tracking : TrackState::Lost, {}
        };
        for (const auto& historyValue : field(track, "history", QJsonValue::Array).toArray())
        {
            const auto point = asObject(historyValue, "history");
            data.history.append(TrackPoint{number(point, "angle_deg"), number(point, "distance_m"),
                                           integer(point, "timestamp_utc_ms")});
        }
        frame.tracks.append(std::move(data));
    }

    QSet<int> detectedIds;
    for (const auto& value : field(object, "detected_ids", QJsonValue::Array).toArray())
    {
        const int id = modelInt(value, "detected_ids");
        expect(targetIds.contains(id) && !detectedIds.contains(id),
               QStringLiteral("Unknown or duplicate detected target ID"));
        detectedIds.insert(id);
        frame.detectedTargetIds.append(id);
    }
    return frame;
}

QString errorAt(qint64 offset, const QString& message)
{
    return QStringLiteral("Byte %1: %2").arg(offset).arg(message);
}
}

void RecordingReader::close()
{
    file.close();
    std::vector<IndexEntry>().swap(index);
    count = 0;
    durationUs = 0;
    recovered = false;
    error.clear();
    recovery.clear();
}

bool RecordingReader::invalidate(const QString& reason)
{
    close();
    error = reason;
    return false;
}

bool RecordingReader::unchanged() const
{
    const QFileInfo pathInfo(file.fileName());
    return pathInfo.isFile() && pathInfo.size() == originalSize &&
           pathInfo.lastModified() == modified && file.size() == originalSize;
}

bool RecordingReader::open(const QString& path, Mode mode)
{
    close();
    const QFileInfo info(path);
    if (!info.isFile())
        return invalidate(QStringLiteral("Recording must be a regular file"));
    file.setFileName(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return invalidate(file.errorString());
    originalSize = file.size();
    modified = QFileInfo(file.fileName()).lastModified();

    try
    {
        checkHeader(readRecord(file));
    }
    catch (const ParseError& problem)
    {
        return invalidate(errorAt(0, problem.message));
    }

    qint64 previousTime = -1;
    bool complete = false;
    constexpr size_t MaxEntries = MaxIndexBytes / sizeof(IndexEntry);
    for (;;)
    {
        const qint64 offset = file.pos();
        try
        {
            expect(offset < originalSize, QStringLiteral("Missing end record"));
            const auto object = readRecord(file);
            if (object.value("type") == "end")
            {
                expect(count > 0 && integer(object, "frame_count", 1) == count,
                       QStringLiteral("End frame count mismatch"));
                const auto duration = integer(object, "duration_us", 0);
                expect(duration >= previousTime && file.pos() == originalSize,
                       QStringLiteral("Invalid end duration or trailing data"));
                durationUs = duration;
                complete = true;
                break;
            }
            const auto frame = parseFrame(object);
            expect(frame.sequence == static_cast<quint64>(count),
                   QStringLiteral("Frame sequence is not consecutive"));
            expect(frame.elapsedMicroseconds > previousTime &&
                   (count != 0 || frame.elapsedMicroseconds == 0),
                   QStringLiteral("Invalid frame timestamp order"));
            if (frame.sequence % IndexStride == 0)
            {
                if (index.size() == MaxEntries)
                    return invalidate(errorAt(offset, QStringLiteral("Index exceeds 32 MiB")));
                if (index.size() == index.capacity())
                    index.reserve(std::min(MaxEntries, std::max(size_t{1}, index.size() * 2)));
                index.push_back({frame.sequence, frame.elapsedMicroseconds, offset});
            }
            previousTime = frame.elapsedMicroseconds;
            ++count;
        }
        catch (const ParseError& problem)
        {
            const auto message = errorAt(offset, problem.message);
            if (file.error() != QFileDevice::NoError)
                return invalidate(errorAt(offset, file.errorString()));
            if (mode == Mode::Strict || count == 0)
                return invalidate(message);
            recovery = message;
            break;
        }
    }

    if (!unchanged())
        return invalidate(QStringLiteral("Recording changed while opening"));
    recovered = !complete;
    if (recovered)
        durationUs = previousTime;
    return true;
}

std::optional<RadarFrame> RecordingReader::frameAtSequence(quint64 sequence)
{
    if (sequence >= static_cast<quint64>(count))
        return std::nullopt;
    const auto next = std::upper_bound(index.begin(), index.end(), sequence,
        [](quint64 value, const IndexEntry& entry) { return value < entry.sequence; });
    return readSegment(static_cast<size_t>(next - index.begin() - 1), sequence, std::nullopt);
}

std::optional<RadarFrame> RecordingReader::frameAtTime(qint64 elapsedMicroseconds)
{
    if (count == 0 || elapsedMicroseconds < 0)
        return std::nullopt;
    const auto next = std::upper_bound(index.begin(), index.end(), elapsedMicroseconds,
        [](qint64 value, const IndexEntry& entry) { return value < entry.elapsedUs; });
    return readSegment(static_cast<size_t>(next - index.begin() - 1),
                       static_cast<quint64>(count - 1), elapsedMicroseconds);
}

std::optional<RadarFrame> RecordingReader::readSegment(size_t segment, quint64 sequence,
                                                        std::optional<qint64> time)
{
    error.clear();
    const auto entry = index[segment];
    if (!unchanged() || !file.seek(entry.offset))
    {
        invalidate(QStringLiteral("Recording changed or could not be positioned"));
        return std::nullopt;
    }

    const auto limit = std::min({sequence + 1, entry.sequence + IndexStride,
                                 static_cast<quint64>(count)});
    std::optional<RadarFrame> result;
    for (quint64 expected = entry.sequence; expected < limit; ++expected)
    {
        const auto offset = file.pos();
        try
        {
            auto frame = parseFrame(readRecord(file));
            expect(frame.sequence == expected &&
                   (result ? frame.elapsedMicroseconds > result->elapsedMicroseconds
                           : frame.elapsedMicroseconds == entry.elapsedUs),
                   QStringLiteral("Frame no longer matches recording index"));
            if (time && frame.elapsedMicroseconds > *time)
                break;
            result = std::move(frame);
        }
        catch (const ParseError& problem)
        {
            invalidate(errorAt(offset, file.error() == QFileDevice::NoError
                                           ? problem.message : file.errorString()));
            return std::nullopt;
        }
    }
    if (!unchanged())
    {
        invalidate(QStringLiteral("Recording changed during frame read"));
        return std::nullopt;
    }
    return result;
}
