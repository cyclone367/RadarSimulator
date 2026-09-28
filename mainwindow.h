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
    void updateRadarStatus();          // 更新雷达状态显示
    void updateTrackTable();           // 更新目标跟踪列表
    void updateTrackInspector();       // 更新目标详细信息面板

    void selectTrackFromTable(int row);       // 根据表格行选择目标
    void selectTrackFromRadar(int targetId);  // 根据目标ID选择目标

    Ui::MainWindow *ui;                // UI界面对象，访问Qt Designer中的控件

    RadarServer *server;               // 雷达TCP服务器对象，负责网络通信
    RadarWidget *radarWidget;          // 雷达显示控件，负责绘制雷达扫描界面
    QTimer statusTimer;                // 状态更新定时器，定期刷新界面数据
};