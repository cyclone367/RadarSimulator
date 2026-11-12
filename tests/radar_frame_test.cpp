#include <cmath>
#include <iostream>

#include <QCoreApplication>
#include <QTimer>

#include "../RadarServer.h"

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "[FAIL] " << message << '\n';
    return condition;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    const RadarFrame empty;
    if (!check(empty.targets.isEmpty() && empty.tracks.isEmpty() &&
                   empty.detectedTargetIds.isEmpty() && !empty.scan.running &&
                   !empty.scan.scanning && empty.sequence == 0 &&
                   empty.elapsedMicroseconds == 0,
               "Default frame is empty and initialized"))
        return 1;

    RadarFrame single;
    single.targets.append(RadarTarget{42, 359.5, -2.0, -3.0, -0.5});
    const RadarFrame singleCopy = single;
    single.targets[0].distance = 0.0;
    if (!check(singleCopy.targets.size() == 1 && singleCopy.targets[0].id == 42 &&
                   singleCopy.targets[0].distance == -2.0 &&
                   singleCopy.targets[0].angularSpeed == -0.5,
               "Single target preserves signed model values and value semantics"))
        return 1;

    RadarServer server;
    const RadarFrame initial = server.snapshot();
    const RadarFrame idle = server.snapshot();
    if (!check(initial.sequence == 0 && initial.elapsedMicroseconds == 0 &&
                   idle.sequence == 1 && idle.elapsedMicroseconds > 0 &&
                   initial.targets.size() == 3 && initial.tracks.isEmpty() &&
                   initial.detectedTargetIds.isEmpty() && initial.scan.running &&
                   !initial.scan.scanning && initial.scan.angleDegrees == 0.0 &&
                   initial.scan.direction == ScanDirection::Clockwise &&
                   initial.scan.speedDegreesPerSecond == 10.0 &&
                   initial.scan.maxRangeMeters == 5000.0 &&
                   initial.scan.beamWidthDegrees == 20.0,
               "Live idle frames capture the default model and ordered metadata"))
        return 1;

    RadarFrame beforeWrap;
    int completedSteps = 0;
    QObject::connect(&server, &RadarServer::simulationStepCompleted, &app, [&] {
        const RadarFrame frame = server.snapshot();
        if (++completedSteps == 1)
        {
            beforeWrap = frame;
            server.setScanSpeed(10.0);
            server.setBeamWidth(20.0);
            return;
        }

        server.stopScan();
        const RadarFrame stopped = server.snapshot();
        const bool passed =
            check(beforeWrap.scan.angleDegrees == 359.0 &&
                      beforeWrap.scan.beamWidthDegrees == 360.0 &&
                      beforeWrap.scan.speedDegreesPerSecond == 3590.0 &&
                      frame.scan.angleDegrees == 0.0 && frame.scan.scanning &&
                      !stopped.scan.scanning && stopped.scan.angleDegrees == 0.0,
                  "Completed frames capture scan wrap and stop state") &&
            check(beforeWrap.sequence == 2 && frame.sequence == 3 &&
                      stopped.sequence == 4 &&
                      idle.elapsedMicroseconds < beforeWrap.elapsedMicroseconds &&
                      beforeWrap.elapsedMicroseconds < frame.elapsedMicroseconds &&
                      frame.elapsedMicroseconds < stopped.elapsedMicroseconds,
                  "Capture sequence and relative timestamps increase") &&
            check(beforeWrap.targets.size() == 3 && beforeWrap.tracks.size() == 3 &&
                      beforeWrap.detectedTargetIds == QList<int>{1, 2, 3} &&
                      beforeWrap.targets[0].state == TargetState::Detected &&
                      frame.detectedTargetIds.isEmpty() &&
                      frame.targets[0].state == TargetState::Outside &&
                      beforeWrap.targets[0].distance == 1002.0 &&
                      frame.targets[0].distance == 1004.0 &&
                      std::abs(frame.targets[0].angle - 45.1) < 1e-9 &&
                      initial.targets[0].distance == 1000.0,
                  "Complete frames preserve targets, motion, tracks and detections");
        if (!passed)
        {
            app.exit(1);
            return;
        }

        RadarFrame edited = stopped;
        edited.tracks[0].history[0].distance = -1.0;
        edited.targets[0].distance = -1.0;
        const RadarFrame fresh = server.snapshot();
        app.exit(check(fresh.tracks[0].history[0].distance == 1002.0 &&
                           fresh.targets[0].distance == 1004.0 &&
                           beforeWrap.tracks[0].history[0].distance == 1002.0,
                       "Nested history and target edits cannot change the live model")
                     ? 0 : 1);
    });

    QTimer::singleShot(5000, &app, [&] {
        check(false, "Timed out waiting for complete simulation steps");
        app.exit(1);
    });
    server.setBeamWidth(360.0);
    server.setScanSpeed(3590.0);
    server.startScan();
    return app.exec();
}
