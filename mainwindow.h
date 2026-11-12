#pragma once
#include <QTimer>
#include <QMainWindow>

#include "RadarFrame.h"

class RadarServer;
class RadarWidget;
class Recorder;
class ReplayController;
class QCloseEvent;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

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

    bool startRecording(const QString& path);
    bool openRecording(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    enum class DisplayMode { Live, Replay };
    enum class RecordingState { Idle, Recording, Saving };

    void setupRecordingControls();
    void stopRecording();
    void recordingFinished(const QString& message);
    void returnToLive();
    void updateRecordingControls();
    void updateReplayTime();
    void refreshDisplay();
    void displayFrame(const RadarFrame& frame);
    void updateRadarStatus();          // 更新雷达状态显示
    void updateTrackTable();           // 更新目标跟踪列表
    void updateTrackInspector();       // 更新目标详细信息面板

    void selectTrackFromTable();              // 根据表格行选择目标
    void selectTrackFromRadar(int targetId);  // 根据目标ID选择目标

    Ui::MainWindow *ui;                // UI界面对象，访问Qt Designer中的控件

    RadarServer *server;               // 雷达TCP服务器对象，负责网络通信
    RadarWidget *radarWidget;          // 雷达显示控件，负责绘制雷达扫描界面
    RadarFrame displayedFrame;
    int selectedTargetId = -1;
    QTimer statusTimer;                // 状态更新定时器，定期刷新界面数据
    Recorder* recorder;
    ReplayController* replay;
    DisplayMode displayMode = DisplayMode::Live;
    RecordingState recordingState = RecordingState::Idle;
    bool closeAfterSave = false;

    QPushButton* recordButton;
    QPushButton* saveButton;
    QPushButton* openButton;
    QPushButton* liveButton;
    QPushButton* playButton;
    QPushButton* pauseButton;
    QPushButton* resumeButton;
    QPushButton* stopButton;
    QPushButton* stepButton;
    QComboBox* speedBox;
    QSlider* timeline;
    QLabel* operationState;
    QLabel* replayTime;
};
