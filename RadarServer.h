#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>
#include <QTimer>
#include <QDateTime>

#include "shared/LineBuffer.hpp"
#include "shared/LineProtocol.hpp"
#include "RadarSimulation.h"

class RadarServer : public QObject
{
    Q_OBJECT

public:
    explicit RadarServer(QObject *parent = nullptr);

    bool start(quint16 port);

    const QList<RadarTrack>& getTracks() const;

    double getScanAngle() const;
    double getMaxDetectionRange() const;
    double getBeamWidth() const;
    double getScanSpeed() const;

    void setBeamWidth(double width);
    void setScanSpeed(double speed);
    void setMaxDetectionRange(double range);

    void startScan();
    void stopScan();

    bool isRadarRunning() const;
    bool isScanning() const;

    const QList<int>& getDetectedTargetIds() const;

signals:
    void trackUpdated();

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    QTcpServer server;
    QList<QTcpSocket*> clients;

    RadarSimulation simulation;

    RadarProtocol::LineBuffer buffer;

    int radarX = 125;
    int radarY = 350;

    bool radarRunning = true;
    bool scanning = false;

    QList<int> detectedTargetIds;

    QTimer scanTimer;
};