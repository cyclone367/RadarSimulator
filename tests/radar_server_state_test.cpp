#include <iostream>

#include <QCoreApplication>
#include <QTcpSocket>
#include <QTimer>

#include "../RadarServer.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    RadarServer server;

    quint16 port = 0;

    for (quint16 candidate = 39000; candidate < 39100; ++candidate)
    {
        if (server.start(candidate))
        {
            port = candidate;
            break;
        }
    }

    if (port == 0)
    {
        std::cerr << "[FAIL] Could not start RadarServer\n";
        return 1;
    }

    QTcpSocket socket;
    QTimer timeout;
    timeout.setSingleShot(true);

    enum class Stage
    {
        WaitingForStartScan,
        WaitingForStop
    };

    Stage stage = Stage::WaitingForStartScan;
    QByteArray receivedData;
    bool completed = false;

    auto fail = [&](const char *message)
    {
        if (completed)
            return;

        completed = true;
        std::cerr << "[FAIL] " << message << '\n';
        app.exit(1);
    };

    QObject::connect(&timeout,
                     &QTimer::timeout,
                     &app,
                     [&]()
                     {
                         fail("Timed out waiting for server response");
                     });

    QObject::connect(&socket,
                     &QTcpSocket::errorOccurred,
                     &app,
                     [&](QAbstractSocket::SocketError)
                     {
                         fail("TCP client error");
                     });

    QObject::connect(&socket,
                     &QTcpSocket::connected,
                     &app,
                     [&]()
                     {
                         socket.write("START_SCAN\r\n");
                         socket.flush();
                     });

    QObject::connect(&socket,
                     &QTcpSocket::readyRead,
                     &app,
                     [&]()
                     {
                         receivedData.append(socket.readAll());

                         while (true)
                         {
                             qsizetype endOfLine =
                                 receivedData.indexOf("\r\n");

                             if (endOfLine < 0)
                                 return;

                             QByteArray line =
                                 receivedData.left(endOfLine);

                             receivedData.remove(0, endOfLine + 2);

                             if (line.startsWith("TARGET "))
                                 continue;

                             if (stage == Stage::WaitingForStartScan)
                             {
                                 if (line != "STATUS SCANNING")
                                 {
                                     fail("Unexpected START_SCAN response");
                                     return;
                                 }

                                 if (!server.isRadarRunning() ||
                                     !server.isScanning())
                                 {
                                     fail("START_SCAN did not enter running/scanning state");
                                     return;
                                 }

                                 stage = Stage::WaitingForStop;
                                 socket.write("STOP\r\n");
                                 socket.flush();
                                 continue;
                             }

                             if (line != "STATUS STOPPED")
                             {
                                 fail("Unexpected STOP response");
                                 return;
                             }

                             if (server.isRadarRunning() ||
                                 server.isScanning())
                             {
                                 fail("STOP did not enter stopped/idle state");
                                 return;
                             }

                             completed = true;
                             std::cout
                                 << "[PASS] STOP stops an active scan\n";
                             app.exit(0);
                             return;
                         }
                     });

    timeout.start(5000);
    socket.connectToHost("127.0.0.1", port);

    return app.exec();
}
