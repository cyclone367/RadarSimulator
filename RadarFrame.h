#pragma once

#include "RadarSimulation.h"

enum class ScanDirection
{
    Clockwise,
    Counterclockwise
};

struct RadarScanState
{
    bool running = false;
    bool scanning = false;
    double angleDegrees = 0.0; // Azimuth measured clockwise from north.
    ScanDirection direction = ScanDirection::Clockwise;
    double beamWidthDegrees = 0.0;
    double maxRangeMeters = 0.0;
    double speedDegreesPerSecond = 0.0; // Magnitude, separate from direction.
};

// A value snapshot. Model coordinates, velocities and UTC history timestamps
// retain the units and meanings defined by RadarSimulation.
struct RadarFrame
{
    quint64 sequence = 0;
    qint64 elapsedMicroseconds = 0;
    RadarScanState scan;
    QList<RadarTarget> targets;
    QList<RadarTrack> tracks;
    QList<int> detectedTargetIds;
};
