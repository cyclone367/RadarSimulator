#pragma once

#include <QList>
#include <QDateTime>

enum class TargetState
{
    Outside,    // 目标在雷达最大探测范围之外
    Detected    // 目标已进入雷达探测范围
};

struct RadarTarget
{
    int id;                              // 目标ID，用于唯一标识目标
    double angle;                        // 目标当前方位角，单位：度
    double distance;                     // 目标与雷达之间的距离，单位：米
    double speed;                        // 目标运动速度，单位：米/秒
    double angularSpeed;                 // 目标角速度，单位：度/秒

    TargetState state = TargetState::Outside; // 目标当前状态，默认处于雷达探测范围之外
};

struct RadarDetection
{
    int id;                 // 被检测目标的ID
    double angle;           // 目标方位角，单位：度
    double distance;        // 目标与雷达之间的距离，单位：米
    qint64 timestamp;       // 检测时间戳，单位：毫秒
};

enum class TrackState
{
    Tracking,   // 正在跟踪目标
    Lost        // 目标暂时丢失
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

    double scanAngle = 0.0;              // 当前扫描角度（°）
    double beamWidth = 20.0;             // 波束宽度（°）
    double maxDetectionRange = 5000.0;   // 最大探测距离（m）
    double scanSpeed = 10.0;             // 扫描速度（°/s）
    qint64 trackTimeout = 3000;          // 目标跟踪超时时间（ms）
    int maxHistorySize = 20;             // 最大历史记录条数
};