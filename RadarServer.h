#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>
#include <QTimer>
#include <QDateTime>
#include <QHash>

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

    double getScanAngle() const;             // 获取当前雷达扫描角度，单位：度
    double getMaxDetectionRange() const;     // 获取最大探测距离，单位：米
    double getBeamWidth() const;             // 获取雷达波束宽度，单位：度
    double getScanSpeed() const;              // 获取雷达扫描速度，单位：度/秒

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
    void onClientDisconnected();

private:
    QTcpServer server;                         // TCP服务器，用于监听客户端连接
    QList<QTcpSocket*> clients;                // 已连接的TCP客户端列表

    RadarSimulation simulation;               // 雷达模拟对象，负责雷达扫描和目标模拟

    QHash<QTcpSocket*, RadarProtocol::LineBuffer> clientBuffers;         // 每个客户端独立的TCP数据缓冲区，key为客户端socket

    int radarX = 125;                          // 雷达在界面中的X坐标
    int radarY = 350;                          // 雷达在界面中的Y坐标

    bool radarRunning = true;                  // 雷达系统是否处于运行状态
    bool scanning = false;                     // 雷达当前是否正在扫描

    QList<int> detectedTargetIds;              // 当前扫描周期检测到的目标ID列表

    QTimer scanTimer;                          // 扫描定时器，用于周期性执行雷达扫描
};