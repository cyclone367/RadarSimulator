#include <iostream>

#include <QApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QTableWidget>
#include <QtMath>

#include "../mainwindow.h"
#include "../RadarServer.h"
#include "../RadarWidget.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    RadarServer server;
    MainWindow window(&server);
    auto* radar = window.findChild<RadarWidget*>();
    auto* table = window.findChild<QTableWidget*>("tableTracks");
    auto* timer = window.findChild<QTimer*>(QString(), Qt::FindDirectChildrenOnly);
    if (!radar || !table || !timer)
        return 1;

    const auto label = [&](const char* name) { return window.findChild<QLabel*>(name)->text(); };
    const auto clickRadar = [&](double angle, double distance) {
        const double radius = qMin(radar->width(), radar->height()) / 2 - 20;
        const double r = radius * distance / 5000.0;
        const double azimuth = qDegreesToRadians(angle);
        const QPointF position(radar->width() / 2 + r * qSin(azimuth),
                               radar->height() / 2 - r * qCos(azimuth));
        QMouseEvent click(QEvent::MouseButtonPress, position, radar->mapToGlobal(position),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(radar, &click);
    };
    const auto require = [&](bool condition, const char* message) {
        if (!condition)
        {
            std::cerr << "[FAIL] " << message << '\n';
            app.exit(1);
        }
        return condition;
    };

    window.show();
    timer->stop();
    const auto refresh = [&] { QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection); };
    quint64 sequenceAfterRefresh = 0;
    int steps = 0;
    QObject::connect(&server, &RadarServer::simulationStepCompleted, &window, [&] {
        const auto live = server.snapshot(); // Independent future recorder observation point.
        if (++steps == 1)
        {
            refresh();
            sequenceAfterRefresh = server.snapshot().sequence;
            if (!require(sequenceAfterRefresh == live.sequence + 2 &&
                             table->rowCount() == 3 &&
                             label("lblCurrentAzimuth") == QStringLiteral("Current Azimuth: 359.0°"),
                         "One refresh takes one snapshot and displays the completed step"))
                return;
            server.setBeamWidth(20.0);
            server.setScanSpeed(10.0);
            return;
        }

        if (steps == 2)
        {
            clickRadar(45.05, 1002.0);
            if (!require(live.sequence == sequenceAfterRefresh + 1 &&
                             live.scan.angleDegrees == 0.0 && live.detectedTargetIds.isEmpty() &&
                             table->currentRow() == 0 &&
                             label("lblCurrentAzimuth") == QStringLiteral("Current Azimuth: 359.0°") &&
                             label("lblInspectorDetection") == "Detection: YES",
                         "A step without UI refresh keeps PPI hit testing and details on the old frame"))
                return;

            refresh();
            require(label("lblCurrentAzimuth") == QStringLiteral("Current Azimuth: 0.0°") &&
                        label("lblTargets") == "Targets: 0" &&
                        table->item(0, 1)->text() == "TRACKING" &&
                        label("lblInspectorDetection") == "Detection: NO",
                    "A shared refresh displays scan wrap and detection loss consistently");
        }

        if (!live.tracks.isEmpty() && live.tracks.first().state == TrackState::Lost)
        {
            refresh();
            if (!require(table->item(0, 1)->text() == "LOST" &&
                             label("lblInspectorState") == "State: LOST",
                         "Table and inspector agree when a track times out"))
                return;
            clickRadar(45.05, 1002.0);
            if (!require(table->selectedItems().isEmpty() &&
                             label("lblInspectorTarget") == "Target: -",
                         "PPI no longer hit-tests the lost track and clears the selection"))
                return;
            server.stopScan();
            QTimer::singleShot(0, &window, &QWidget::close);
        }
    });

    QTimer::singleShot(8000, &app, [&] { require(false, "Timed out waiting for live track loss"); });
    server.setBeamWidth(360.0);
    server.setScanSpeed(3590.0);
    server.startScan();
    return app.exec();
}
