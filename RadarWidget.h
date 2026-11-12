#pragma once

#include <QWidget>
#include "RadarFrame.h"

class RadarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RadarWidget(QWidget *parent = nullptr);

    void setFrame(const RadarFrame& frame);
    void setSelectedTargetId(int id);

signals:
    void targetSelected(int targetId);

protected:
    void paintEvent(QPaintEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

private:
    RadarFrame frame;

    // 当前选中的目标 ID
    int selectedTargetId = -1;
};
