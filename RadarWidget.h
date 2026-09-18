#pragma once

#include <QWidget>
#include <QTimer>

class RadarServer;

class RadarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RadarWidget(RadarServer *server,
                         QWidget *parent = nullptr);

    void setSelectedTargetId(int id);

signals:
    void targetSelected(int targetId);

protected:
    void paintEvent(QPaintEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

private:
    RadarServer *server;

    QTimer timer;

    // 当前选中的目标 ID
    int selectedTargetId = -1;
};