#include "RadarWidget.h"
#include "RadarServer.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QMouseEvent>

RadarWidget::RadarWidget(RadarServer *server,
                         QWidget *parent)
    : QWidget(parent)
    , server(server)
{
    setMinimumSize(500, 500);

    timer.setInterval(50);

    connect(&timer,
            &QTimer::timeout,
            this,
            [this]()
            {
                update();
            });

    timer.start();
}

void RadarWidget::setSelectedTargetId(int id)
{
    selectedTargetId = id;
    update();
}

void RadarWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing);

    painter.fillRect(this->rect(), QColor(0, 30, 0));

    // ==================================================
    // 1. 计算雷达显示区域
    // ==================================================

    int radarWidth = width();

    int size = qMin(radarWidth, height());

    int centerX = radarWidth / 2;
    int centerY = height() / 2;

    int radius = size / 2 - 20;

    const double maxDistance =
        server->getMaxDetectionRange();

    // ==================================================
    // 2. 绘制最外层雷达圆
    // ==================================================

    painter.drawEllipse(
        centerX - radius,
        centerY - radius,
        radius * 2,
        radius * 2
        );

    QPen gridPen(QColor(80, 255, 80, 100), 1);
    painter.setPen(gridPen);

    // ==================================================
    // 3. 绘制距离环
    // ==================================================

    for (int i = 1; i <= 4; ++i)
    {
        int r = radius * i / 4;

        painter.drawEllipse(
            centerX - r,
            centerY - r,
            r * 2,
            r * 2
            );

        double distance =
            maxDistance * i / 4.0;

        QString label =
            QString("%1 m")
                .arg(distance, 0, 'f', 0);

        painter.drawText(
            centerX + 5,
            centerY - r - 5,
            label
            );
    }

    // ==================================================
    // 4. 绘制水平线
    // ==================================================

    painter.drawLine(
        centerX - radius,
        centerY,
        centerX + radius,
        centerY
        );

    // ==================================================
    // 5. 绘制垂直线
    // ==================================================

    painter.drawLine(
        centerX,
        centerY - radius,
        centerX,
        centerY + radius
        );

    // ==================================================
    // 6. 绘制方向标签
    // ==================================================

    painter.drawText(
        centerX - 10,
        centerY - radius - 8,
        "0°"
        );

    painter.drawText(
        centerX + radius + 8,
        centerY + 5,
        "90°"
        );

    painter.drawText(
        centerX - 15,
        centerY + radius + 20,
        "180°"
        );

    painter.drawText(
        centerX - radius - 35,
        centerY + 5,
        "270°"
        );

    // ==================================================
    // 7. 绘制雷达中心点
    // ==================================================

    painter.drawEllipse(
        centerX - 4,
        centerY - 4,
        8,
        8
        );

    // ==================================================
    // 8. 绘制真实扫描扇区
    // ==================================================

    double scanAngle =
        server->getScanAngle();

    double beamWidth =
        server->getBeamWidth();

    double sectorWidth =
        qMax(beamWidth, 6.0);

    double halfWidth =
        sectorWidth / 2.0;

    double startAngle =
        90.0 - (scanAngle + halfWidth);

    int startAngle16 =
        qRound(startAngle * 16.0);

    int spanAngle16 =
        qRound(sectorWidth * 16.0);

    painter.setBrush(
        QBrush(QColor(0, 255, 0, 40))
        );

    painter.drawPie(
        centerX - radius,
        centerY - radius,
        radius * 2,
        radius * 2,
        startAngle16,
        spanAngle16
        );

    painter.setBrush(Qt::NoBrush);

    // ==================================================
    // 9. 获取当前 Track
    // ==================================================

    const QList<RadarTrack>& tracks =
        server->getTracks();

    // ==================================================
    // 10. 获取当前正在被扫描到的目标
    // ==================================================

    const QList<int>& detectedIds =
        server->getDetectedTargetIds();

    // ==================================================
    // 11. 绘制所有 Track
    // ==================================================

    for (const RadarTrack& track : tracks)
    {
        // ==================================================
        // 11.1 绘制历史轨迹
        // ==================================================

        if (track.history.size() >= 2)
        {
            QPainterPath path;

            bool firstPoint = true;

            for (const TrackPoint& point : track.history)
            {
                double rad =
                    qDegreesToRadians(point.angle);

                double x =
                    centerX +
                    radius *
                        point.distance /
                        maxDistance *
                        qSin(rad);

                double y =
                    centerY -
                    radius *
                        point.distance /
                        maxDistance *
                        qCos(rad);

                if (firstPoint)
                {
                    path.moveTo(x, y);
                    firstPoint = false;
                }
                else
                {
                    path.lineTo(x, y);
                }
            }

            painter.save();

            bool selected =
                (track.id == selectedTargetId);

            QPen trailPen;

            if (selected)
            {
                trailPen =
                    QPen(QColor(0, 255, 255, 220));

                trailPen.setWidth(3);
            }
            else
            {
                trailPen =
                    QPen(QColor(0, 180, 255, 80));

                trailPen.setWidth(1);
            }

            painter.setPen(trailPen);
            painter.setBrush(Qt::NoBrush);

            painter.drawPath(path);

            for (const TrackPoint& point : track.history)
            {
                double rad =
                    qDegreesToRadians(point.angle);

                double x =
                    centerX +
                    radius *
                        point.distance /
                        maxDistance *
                        qSin(rad);

                double y =
                    centerY -
                    radius *
                        point.distance /
                        maxDistance *
                        qCos(rad);

                painter.drawEllipse(
                    QPointF(x, y),
                    selected ? 3 : 2,
                    selected ? 3 : 2
                    );
            }

            painter.restore();
        }

        // ==================================================
        // 11.2 计算当前目标位置
        // ==================================================

        double targetRadius =
            track.distance / maxDistance * radius;

        if (targetRadius > radius)
            continue;

        double rad =
            qDegreesToRadians(track.angle);

        double x =
            centerX +
            targetRadius * qSin(rad);

        double y =
            centerY -
            targetRadius * qCos(rad);

        QPointF targetPoint(x, y);

        // ==================================================
        // 11.3 判断目标状态
        // ==================================================

        bool detected =
            detectedIds.contains(track.id);

        bool selected =
            (track.id == selectedTargetId);

        // ==================================================
        // 11.4 LOST 目标显示
        // ==================================================

        if (track.state == TrackState::Lost)
        {
            painter.save();

            painter.setPen(
                QPen(
                    QColor(150, 150, 150),
                    2
                    )
                );

            painter.setBrush(Qt::NoBrush);

            // 空心圆
            painter.drawEllipse(
                targetPoint,
                8,
                8
                );

            // X 标记
            painter.drawLine(
                QPointF(x - 5, y - 5),
                QPointF(x + 5, y + 5)
                );

            painter.drawLine(
                QPointF(x - 5, y + 5),
                QPointF(x + 5, y - 5)
                );

            // LOST 标签
            painter.drawText(
                QPointF(x + 10, y - 10),
                QString("T%1 LOST").arg(track.id)
                );

            painter.restore();

            // LOST 目标不继续绘制预测轨迹
            continue;
        }

        // ==================================================
        // 11.5 当前目标选中效果
        // ==================================================

        if (selected)
        {
            painter.save();

            painter.setPen(
                QPen(
                    QColor(0, 255, 255),
                    2
                    )
                );

            painter.setBrush(Qt::NoBrush);

            painter.drawEllipse(
                targetPoint,
                14,
                14
                );

            painter.restore();
        }

        // ==================================================
        // 11.6 绘制当前目标
        // ==================================================

        painter.save();

        if (detected)
        {
            painter.setBrush(
                QColor(0, 255, 0)
                );

            painter.setPen(
                Qt::NoPen
                );

            painter.drawEllipse(
                targetPoint,
                10,
                10
                );
        }
        else
        {
            painter.setBrush(
                QColor(0, 200, 0)
                );

            painter.setPen(
                Qt::NoPen
                );

            painter.drawEllipse(
                targetPoint,
                6,
                6
                );
        }

        painter.restore();

        // ==================================================
        // 11.7 绘制目标编号
        // ==================================================

        painter.save();

        painter.setPen(
            QColor(255, 255, 255)
            );

        painter.drawText(
            QPointF(x + 10, y + 5),
            QString("T%1").arg(track.id)
            );

        painter.restore();

        // ==================================================
        // 11.8 其他信息
        // ==================================================

        // 当前目标绘制完成

        // ==================================================
        // 11.9 只有选中的目标显示预测轨迹
        // ==================================================

        if (selected)
        {
            QList<QPointF> predictionPoints;

            // 当前目标作为预测轨迹起点
            predictionPoints.append(QPointF(x, y));

            // 预测 1 秒、2 秒、3 秒后的位置
            for (int futureTime = 1; futureTime <= 3; futureTime++)
            {
                double futureDistance =
                    track.distance +
                    track.estimatedSpeed * futureTime;

                if (futureDistance < 0 ||
                    futureDistance > maxDistance)
                {
                    break;
                }

                double futureAngle =
                    track.angle +
                    track.estimatedAngularSpeed * futureTime;

                while (futureAngle >= 360.0)
                    futureAngle -= 360.0;

                while (futureAngle < 0.0)
                    futureAngle += 360.0;

                double futureRadius =
                    futureDistance /
                    maxDistance *
                    radius;

                double futureRad =
                    qDegreesToRadians(futureAngle);

                double futureX =
                    centerX +
                    futureRadius *
                        qSin(futureRad);

                double futureY =
                    centerY -
                    futureRadius *
                        qCos(futureRad);

                predictionPoints.append(
                    QPointF(futureX, futureY)
                    );
            }

            // ==================================================
            // 11.10 绘制预测轨迹
            // ==================================================

            if (predictionPoints.size() >= 2)
            {
                painter.save();

                painter.setPen(
                    QPen(
                        QColor(255, 255, 0),
                        1,
                        Qt::DashLine
                        )
                    );

                for (int i = 1;
                     i < predictionPoints.size();
                     i++)
                {
                    painter.drawLine(
                        predictionPoints[i - 1],
                        predictionPoints[i]
                        );
                }

                painter.setPen(
                    QPen(
                        QColor(255, 255, 0),
                        2
                        )
                    );

                painter.setBrush(Qt::NoBrush);

                for (int i = 1;
                     i < predictionPoints.size();
                     i++)
                {
                    painter.drawEllipse(
                        predictionPoints[i],
                        4,
                        4
                        );
                }

                // ==================================================
                // 绘制预测时间标签
                // ==================================================

                for (int i = 1;
                     i < predictionPoints.size();
                     i++)
                {
                    QString timeLabel =
                        QString("+%1s").arg(i);

                    QPointF labelPosition =
                        predictionPoints[i] +
                        QPointF(6, -6);

                    painter.drawText(
                        labelPosition,
                        timeLabel
                        );
                }

                painter.restore();
            }
        }
    }
}

void RadarWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
    {
        return;
    }

    // ==================================================
    // 1. 获取鼠标点击位置
    // ==================================================

    QPointF mousePosition =
        event->position();

    qDebug() << "Mouse clicked:"
             << mousePosition;

    // ==================================================
    // 2. 计算雷达显示区域
    // ==================================================

    int radarWidth =
        width();

    int size =
        qMin(radarWidth, height());

    int centerX =
        radarWidth / 2;

    int centerY =
        height() / 2;

    int radius =
        size / 2 - 20;

    // ==================================================
    // 3. 获取最大探测距离
    // ==================================================

    const double maxDistance =
        server->getMaxDetectionRange();

    // ==================================================
    // 4. 获取所有 Track
    // ==================================================

    const QList<RadarTrack>& tracks =
        server->getTracks();

    // ==================================================
    // 5. 默认取消选择
    // ==================================================

    selectedTargetId = -1;

    // ==================================================
    // 6. 判断鼠标是否点击到了某个目标
    // ==================================================

    for (const RadarTrack& track : tracks)
    {
        if (track.state == TrackState::Lost)
        {
            continue;
        }

        // ------------------------------------------------
        // 把真实距离转换成屏幕半径
        // ------------------------------------------------

        double targetRadius =
            track.distance /
            maxDistance *
            radius;

        if (targetRadius > radius)
        {
            continue;
        }

        // ------------------------------------------------
        // 把雷达角度转换成屏幕坐标
        // ------------------------------------------------

        double rad =
            qDegreesToRadians(track.angle);

        double targetX =
            centerX +
            targetRadius *
                qSin(rad);

        double targetY =
            centerY -
            targetRadius *
                qCos(rad);

        QPointF targetPosition(
            targetX,
            targetY
            );

        // ==================================================
        // 计算鼠标与目标之间的距离
        // ==================================================

        double dx =
            mousePosition.x() -
            targetPosition.x();

        double dy =
            mousePosition.y() -
            targetPosition.y();

        double distance =
            qSqrt(
                dx * dx +
                dy * dy
                );

        // ==================================================
        // 如果距离足够近，就认为点击了目标
        // ==================================================

        if (distance <= 15.0)
        {
            selectedTargetId =
                track.id;

            emit targetSelected(track.id);

            qDebug() << "Target selected:"
                     << selectedTargetId;

            return;
        }
    }

    // ==================================================
    // 7. 没有点击任何目标
    // ==================================================

    qDebug() << "No target selected";
}