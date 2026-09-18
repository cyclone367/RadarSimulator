#include "mainwindow.h"
#include "RadarServer.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    RadarServer server;

    if (!server.start(9000))
    {
        return 1;
    }

    MainWindow w(&server);
    w.show();
    return QApplication::exec();
}
