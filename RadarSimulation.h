#pragma once

#include <QList>
#include <QDateTime>

enum class TargetState
{
    Outside,
    Detected
};

struct RadarTarget
{
    int id;
    double angle;
    double distance;
    double speed;
    double angularSpeed;

    TargetState state = TargetState::Outside;
};

struct RadarDetection
{
    int id;
    double angle;
    double distance;
    qint64 timestamp;
};

enum class TrackState
{
    Tracking,
    Lost
};

struct TrackPoint
{
    double angle;
    double distance;
    qint64 timestamp;
};

struct RadarTrack
{
    int id;
    double angle;
    double distance;
    double estimatedSpeed;
    double estimatedAngularSpeed;
    double predictedDistance;
    double predictedAngle;
    qint64 lastUpdateTime;
    int hitCount;

    TrackState state = TrackState::Tracking;

    QList<TrackPoint> history;
};

class RadarSimulation : public QObject
{
    Q_OBJECT

public:
    explicit RadarSimulation(QObject *parent = nullptr);

    const QList<RadarTarget>& getTargets() const;
    QList<RadarTarget>& getTargets();

    const QList<RadarTrack>& getTracks() const;
    QList<RadarTrack>& getTracks();

    // 仿真参数
    double getScanAngle() const;
    double getBeamWidth() const;
    double getMaxDetectionRange() const;
    double getScanSpeed() const;

    qint64 getTrackTimeout() const;
    int getMaxHistorySize() const;

    void setBeamWidth(double width);
    void setMaxDetectionRange(double range);
    void setScanSpeed(double speed);

    void setTrackTimeout(qint64 timeout);
    void setMaxHistorySize(int size);

    void advanceScan(double deltaAngle);

    void updateSimulation();

    bool isTargetDetected(const RadarTarget& target) const;

    RadarTrack* findTrack(int id);

    void processDetection(const RadarDetection& detection);

    void updateTrackStates();

private:
    QList<RadarTarget> targets;
    QList<RadarTrack> tracks;

    // 雷达仿真参数
    double scanAngle = 0.0;

    // double beamWidth = 1.0;
    // double beamWidth = 6.0;
    double beamWidth = 20.0;
    double maxDetectionRange = 5000.0;
    double scanSpeed = 10.0;

    qint64 trackTimeout = 3000;
    int maxHistorySize = 20;
};