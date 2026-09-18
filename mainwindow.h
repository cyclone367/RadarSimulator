#pragma once
#include <QTimer>
#include <QMainWindow>

class RadarServer;
class RadarWidget;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(RadarServer *server,
                        QWidget *parent = nullptr);
    ~MainWindow();

private:
    void updateRadarStatus();
    void updateTrackTable();
    void updateTrackInspector();

    void selectTrackFromTable(int row);
    void selectTrackFromRadar(int targetId);

    Ui::MainWindow *ui;

    RadarServer *server;
    RadarWidget *radarWidget;
    QTimer statusTimer;
};