#include "RadarServer.h"

#include <QDebug>

RadarServer::RadarServer(QObject *parent)
    : QObject(parent)
{
    connect(&server,
            &QTcpServer::newConnection,
            this,
            &RadarServer::onNewConnection);

    scanTimer.setInterval(100);

    connect(&scanTimer,
            &QTimer::timeout,
            this,
            [this]()
            {
                if (!scanning)
                {
                    return;
                }

                // =========================
                // 1. 雷达扫描角度更新
                // =========================
                simulation.updateSimulation();

                simulation.advanceScan(
                    simulation.getScanSpeed() * 0.1
                    );

                detectedTargetIds.clear();

                qDebug() << "Radar angle:"
                         << simulation.getScanAngle();


                // =========================
                // 2. 更新所有目标
                // =========================

                for (RadarTarget& target : simulation.getTargets())
                {
                    qDebug() << "Target"
                             << target.id
                             << "angle:"
                             << target.angle
                             << "distance:"
                             << target.distance
                             << "speed:"
                             << target.speed
                             << "angularSpeed:"
                             << target.angularSpeed;


                    // =========================
                    // 3. 判断是否被雷达波束照射
                    // =========================

                    bool detectedNow =
                        simulation.isTargetDetected(target);

                    if (detectedNow)
                    {
                        detectedTargetIds.append(target.id);

                        // =========================
                        // 每次检测都生成一次 Detection
                        // =========================

                        RadarDetection detection;

                        detection.id = target.id;
                        detection.angle = target.angle;
                        detection.distance = target.distance;

                        detection.timestamp =
                            QDateTime::currentMSecsSinceEpoch();

                        simulation.processDetection(detection);

                        emit trackUpdated();

                        // =========================
                        // 第一次进入探测条件
                        // =========================

                        if (target.state == TargetState::Outside)
                        {
                            target.state = TargetState::Detected;

                            qDebug() << "TARGET DETECTED!";
                            qDebug() << "ID:"
                                     << detection.id;

                            qDebug() << "Angle:"
                                     << detection.angle;

                            qDebug() << "Distance:"
                                     << detection.distance;

                            qDebug() << "Timestamp:"
                                     << detection.timestamp;

                            QByteArray response =
                                QString("TARGET %1 %2 %3\r\n")
                                    .arg(detection.id)
                                    .arg(detection.angle)
                                    .arg(detection.distance)
                                    .toUtf8();

                            for (QTcpSocket *client : clients)
                            {
                                if (client->state() ==
                                    QAbstractSocket::ConnectedState)
                                {
                                    client->write(response);
                                    client->flush();
                                }
                            }

                            qDebug() << "Sent:"
                                     << response;
                        }
                    }
                    else
                    {
                        if (target.state == TargetState::Detected)
                        {
                            // =========================
                            // 目标离开探测条件
                            // =========================

                            qDebug() << "TARGET LOST!";

                            qDebug() << "ID:"
                                     << target.id;

                            qDebug() << "Angle:"
                                     << target.angle;

                            qDebug() << "Distance:"
                                     << target.distance;

                            target.state = TargetState::Outside;
                        }
                    }
                }

                simulation.updateTrackStates();
            });
}

const QList<RadarTrack>& RadarServer::getTracks() const
{
    return simulation.getTracks();
}

double RadarServer::getScanAngle() const
{
    return simulation.getScanAngle();
}

double RadarServer::getBeamWidth() const
{
    return simulation.getBeamWidth();
}

void RadarServer::setBeamWidth(double width)
{
    if (width <= 0.0)
        return;

    simulation.setBeamWidth(width);
}

double RadarServer::getScanSpeed() const
{
    return simulation.getScanSpeed();
}

void RadarServer::setScanSpeed(double speed)
{
    if (speed <= 0.0)
        return;

    simulation.setScanSpeed(speed);
}

double RadarServer::getMaxDetectionRange() const
{
    return simulation.getMaxDetectionRange();
}

void RadarServer::setMaxDetectionRange(double range)
{
    if (range <= 0.0)
        return;

    simulation.setMaxDetectionRange(range);
}

const QList<int>& RadarServer::getDetectedTargetIds() const
{
    return detectedTargetIds;
}

bool RadarServer::start(quint16 port)
{
    if (!server.listen(QHostAddress::Any, port))
    {
        qDebug() << "Radar server start failed:"
                 << server.errorString();

        return false;
    }

    qDebug() << "Radar server listening on port"
             << port;

    return true;
}

void RadarServer::onNewConnection()
{
    QTcpSocket *socket = server.nextPendingConnection();

    if (socket == nullptr)
    {
        return;
    }

    clients.append(socket);

    qDebug() << "New client connected!";
    qDebug() << "Client address:" << socket->peerAddress().toString();
    qDebug() << "Client port:" << socket->peerPort();

    connect(socket,
            &QTcpSocket::readyRead,
            this,
            &RadarServer::onReadyRead);
}

void RadarServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());

    if (socket == nullptr)
    {
        return;
    }

    QByteArray data = socket->readAll();

    buffer.append(data.toStdString());

    auto lines = buffer.takeLines();

    for (const std::string& line : lines)
    {
        RadarProtocol::Frame frame =
            RadarProtocol::parseLine(line);

        qDebug() << "Received command:"
                 << QString::fromStdString(
                        RadarProtocol::commandToString(frame.command));

        if (frame.command == RadarProtocol::Command::GetStatus)
        {
            QByteArray response;

            qDebug() << "radarRunning =" << radarRunning;

            if (radarRunning)
            {
                response = "STATUS RUNNING\r\n";
            }
            else
            {
                response = "STATUS STOPPED\r\n";
            }

            socket->write(response);
            socket->flush();

            qDebug() << "Sent:" << response;
        }

        if (frame.command == RadarProtocol::Command::GetPosition)
        {
            QString response =
                QString("POS %1 %2\r\n")
                    .arg(radarX)
                    .arg(radarY);

            socket->write(response.toUtf8());
            socket->flush();

            qDebug() << "Sent:" << response;
        }

        if (frame.command == RadarProtocol::Command::Move)
        {
            if (!radarRunning)
            {
                QByteArray response = "ERROR RADAR_STOPPED\r\n";

                socket->write(response);
                socket->flush();

                qDebug() << "Cannot move: radar is stopped";
                qDebug() << "Sent:" << response;

                continue;
            }

            if (frame.params.size() != 2)
            {
                QByteArray response = "ERROR INVALID_PARAMS\r\n";

                socket->write(response);
                socket->flush();

                qDebug() << "Sent:" << response;

                continue;
            }

            radarX = std::stoi(frame.params[0]);
            radarY = std::stoi(frame.params[1]);

            QByteArray response = "STATUS MOVED\r\n";

            socket->write(response);
            socket->flush();

            qDebug() << "Radar moved to:"
                     << radarX
                     << radarY;

            qDebug() << "Sent:" << response;
        }

        if (frame.command == RadarProtocol::Command::Stop)
        {
            radarRunning = false;

            QByteArray response = "STATUS STOPPED\r\n";

            socket->write(response);
            socket->flush();

            qDebug() << "Radar stopped";
            qDebug() << "radarRunning =" << radarRunning;
            qDebug() << "Sent:" << response;
        }

        if (frame.command == RadarProtocol::Command::Start)
        {
            radarRunning = true;

            QByteArray response = "STATUS STARTED\r\n";

            socket->write(response);
            socket->flush();

            qDebug() << "Radar started";
            qDebug() << "radarRunning =" << radarRunning;
            qDebug() << "Sent:" << response;
        }

        if (frame.command == RadarProtocol::Command::StartScan)
        {
            if (!radarRunning)
            {
                QByteArray response = "ERROR RADAR_STOPPED\r\n";

                socket->write(response);
                socket->flush();

                qDebug() << "Cannot start scan: radar is stopped";
                qDebug() << "Sent:" << response;

                continue;
            }

            startScan();

            QByteArray response = "STATUS SCANNING\r\n";

            socket->write(response);
            socket->flush();

            qDebug() << "Radar scanning";
            qDebug() << "scanning =" << scanning;
            qDebug() << "Sent:" << response;
        }

        if (frame.command == RadarProtocol::Command::StopScan)
        {
            stopScan();

            QByteArray response = "STATUS SCAN_STOPPED\r\n";

            socket->write(response);
            socket->flush();

            qDebug() << "Radar scan stopped";
            qDebug() << "scanning =" << scanning;
            qDebug() << "Sent:" << response;
        }
    }
}

void RadarServer::startScan()
{
    if (!radarRunning)
        return;

    scanning = true;

    scanTimer.start();

    qDebug() << "RadarServer scan timer started:"
             << scanTimer.isActive();
}

void RadarServer::stopScan()
{
    scanning = false;
    scanTimer.stop();
}

bool RadarServer::isRadarRunning() const
{
    return radarRunning;
}

bool RadarServer::isScanning() const
{
    return scanning;
}