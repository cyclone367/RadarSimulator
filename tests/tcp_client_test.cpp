#include <iostream>

#include <QTcpSocket>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTcpSocket socket;

    std::cout << "Connecting to RadarSimulator...\n";

    socket.connectToHost("127.0.0.1", 9000);

    if (!socket.waitForConnected(3000))
    {
        std::cout << "Connection failed: "
                  << socket.errorString().toStdString()
                  << '\n';

        return 1;
    }

    std::cout << "Connected successfully!\n";

    // QByteArray message = "START_SCAN\r\n";
    QByteArray message = "STOP_SCAN\r\n";


    socket.write(message);
    socket.flush();

    // std::cout << "Sent: START_SCAN\\r\\n\n";
    std::cout << "Sent: STOP_SCAN\\r\\n\n";

    while (true)
    {
        if (!socket.waitForReadyRead(10000))
        {
            std::cout << "No response received.\n";
            break;
        }

        QByteArray response = socket.readAll();

        std::cout << "Received: "
                  << response.toStdString();

        break;
    }

    socket.disconnectFromHost();

    return 0;
}
