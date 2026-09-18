#include "RadarSimulation.h"

#include <QtMath>
#include <QDebug>

RadarSimulation::RadarSimulation(QObject *parent)
    : QObject(parent)
{
    targets = {
        {1, 45.0, 1000.0, 20.0, 0.5},
        {2, 120.0, 2000.0, -15.0, -0.3},
        {3, 270.0, 1500.0, 30.0, 1.0}
    };
}

const QList<RadarTarget>& RadarSimulation::getTargets() const
{
    return targets;
}

QList<RadarTarget>& RadarSimulation::getTargets()
{
    return targets;
}

const QList<RadarTrack>& RadarSimulation::getTracks() const
{
    return tracks;
}

QList<RadarTrack>& RadarSimulation::getTracks()
{
    return tracks;
}

double RadarSimulation::getScanAngle() const
{
    return scanAngle;
}

double RadarSimulation::getBeamWidth() const
{
    return beamWidth;
}

double RadarSimulation::getMaxDetectionRange() const
{
    return maxDetectionRange;
}

double RadarSimulation::getScanSpeed() const
{
    return scanSpeed;
}

qint64 RadarSimulation::getTrackTimeout() const
{
    return trackTimeout;
}

int RadarSimulation::getMaxHistorySize() const
{
    return maxHistorySize;
}

void RadarSimulation::setBeamWidth(double width)
{
    beamWidth = width;
}

void RadarSimulation::setMaxDetectionRange(double range)
{
    maxDetectionRange = range;
}

void RadarSimulation::setScanSpeed(double speed)
{
    scanSpeed = speed;
}

void RadarSimulation::advanceScan(double deltaAngle)
{
    scanAngle += deltaAngle;

    if (scanAngle >= 360.0)
        scanAngle -= 360.0;
}

void RadarSimulation::setTrackTimeout(qint64 timeout)
{
    trackTimeout = timeout;
}

void RadarSimulation::setMaxHistorySize(int size)
{
    maxHistorySize = size;
}

void RadarSimulation::updateSimulation()
{
    // 更新所有目标的位置
    for (RadarTarget& target : targets)
    {
        target.distance += target.speed * 0.1;
        target.angle += target.angularSpeed * 0.1;

        // 保证角度始终处于 0 ~ 360 度
        while (target.angle >= 360.0)
            target.angle -= 360.0;

        while (target.angle < 0.0)
            target.angle += 360.0;

        qDebug() << "SIM Target"
                 << target.id
                 << "angle:" << target.angle
                 << "distance:" << target.distance;
    }
}

bool RadarSimulation::isTargetDetected(
    const RadarTarget& target) const
{
    double diff =
        qAbs(scanAngle - target.angle);

    if (diff > 180.0)
        diff = 360.0 - diff;

    bool insideBeam =
        diff <= beamWidth / 2.0;

    bool insideRange =
        target.distance <= maxDetectionRange;

    return insideBeam && insideRange;
}

RadarTrack* RadarSimulation::findTrack(int id)
{
    for (RadarTrack& track : tracks)
    {
        if (track.id == id)
            return &track;
    }

    return nullptr;
}

void RadarSimulation::processDetection(
    const RadarDetection& detection)
{
    qDebug() << "===== processDetection called =====";

    RadarTrack* track = findTrack(detection.id);

    if (track == nullptr)
    {
        RadarTrack newTrack;

        newTrack.id = detection.id;
        newTrack.angle = detection.angle;
        newTrack.distance = detection.distance;
        newTrack.estimatedSpeed = 0.0;
        newTrack.estimatedAngularSpeed = 0.0;

        newTrack.predictedDistance =
            detection.distance;

        newTrack.predictedAngle =
            detection.angle;
        newTrack.lastUpdateTime = detection.timestamp;
        newTrack.hitCount = 1;

        TrackPoint point;

        point.angle = detection.angle;
        point.distance = detection.distance;
        point.timestamp = detection.timestamp;

        newTrack.history.append(point);

        tracks.append(newTrack);

        qDebug() << "TRACK CREATED";
        qDebug() << "ID:" << newTrack.id;
    }
    else
    {
        bool wasLost =
            (track->state == TrackState::Lost);

        if (!wasLost)
        {
            double distanceDifference =
                detection.distance -
                track->distance;

            double angleDifference =
                detection.angle -
                track->angle;

            // 处理 0° / 360° 跨界
            if (angleDifference > 180.0)
            {
                angleDifference -= 360.0;
            }

            if (angleDifference < -180.0)
            {
                angleDifference += 360.0;
            }

            qint64 timeDifference =
                detection.timestamp -
                track->lastUpdateTime;

            qDebug() << "wasLost:"
                     << wasLost;

            qDebug() << "Current distance:"
                     << detection.distance;

            qDebug() << "Previous distance:"
                     << track->distance;

            qDebug() << "Distance difference:"
                     << distanceDifference;

            qDebug() << "Current timestamp:"
                     << detection.timestamp;

            qDebug() << "Previous timestamp:"
                     << track->lastUpdateTime;

            qDebug() << "Time difference:"
                     << timeDifference;

            if (timeDifference > 0)
            {
                track->estimatedSpeed =
                    distanceDifference /
                    (timeDifference / 1000.0);

                track->estimatedAngularSpeed =
                    angleDifference /
                    (timeDifference / 1000.0);

                qDebug() << "Calculated speed:"
                         << track->estimatedSpeed;

                qDebug() << "Calculated angular speed:"
                         << track->estimatedAngularSpeed;

                track->predictedDistance =
                    track->distance +
                    track->estimatedSpeed * 3.0;

                track->predictedAngle =
                    track->angle +
                    track->estimatedAngularSpeed * 3.0;
            }
        }
        else
        {
            track->estimatedSpeed = 0.0;
            track->estimatedAngularSpeed = 0.0;
        }

        track->angle =
            detection.angle;

        track->distance =
            detection.distance;

        track->lastUpdateTime =
            detection.timestamp;

        track->predictedDistance =
            track->distance +
            track->estimatedSpeed * 1.0;

        track->hitCount++;

        track->state =
            TrackState::Tracking;

        TrackPoint point;

        point.angle =
            detection.angle;

        point.distance =
            detection.distance;

        point.timestamp =
            detection.timestamp;

        track->history.append(point);

        if (track->history.size() > maxHistorySize)
        {
            track->history.removeFirst();
        }

        qDebug() << "TRACK UPDATED";

        qDebug() << "ID:"
                 << track->id;

        qDebug() << "Hit count:"
                 << track->hitCount;

        qDebug() << "Estimated speed:"
                 << track->estimatedSpeed;

        qDebug() << "History size:"
                 << track->history.size();
    }
}

void RadarSimulation::updateTrackStates()
{
    qint64 now =
        QDateTime::currentMSecsSinceEpoch();

    for (RadarTrack& track : tracks)
    {
        if (track.state == TrackState::Tracking &&
            now - track.lastUpdateTime > trackTimeout)
        {
            track.state = TrackState::Lost;

            track.predictedDistance = track.distance;
            track.predictedAngle = track.angle;

            qDebug() << "TRACK LOST";
            qDebug() << "ID:" << track.id;
            qDebug() << "Last angle:" << track.angle;
            qDebug() << "Last distance:" << track.distance;
            qDebug() << "Hit count:" << track.hitCount;
        }
    }
}