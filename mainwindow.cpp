#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "RadarServer.h"
#include "RadarWidget.h"

#include <QVBoxLayout>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QHeaderView>
#include <QBrush>
#include <QColor>
#include <QAbstractItemView>
#include <QDateTime>

MainWindow::MainWindow(RadarServer *server,
                       QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , server(server)
    , radarWidget(new RadarWidget(server))
{
    ui->setupUi(this);

    // 设置左右区域比例：雷达区 7，控制区 3
    ui->horizontalLayout->setStretch(0, 7);
    ui->horizontalLayout->setStretch(1, 3);

    ui->tableTracks->setHorizontalHeaderLabels({
        "ID",
        "STATE",
        "AZIMUTH",
        "RANGE",
        "SPEED",
        "HITS",
        "HISTORY"
    });

    ui->tableTracks->horizontalHeader()->setStretchLastSection(true);
    ui->tableTracks->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch
        );

    ui->tableTracks->setEditTriggers(
        QAbstractItemView::NoEditTriggers
        );

    ui->tableTracks->setSelectionBehavior(
        QAbstractItemView::SelectRows
        );

    ui->tableTracks->setSelectionMode(
        QAbstractItemView::SingleSelection
        );

    ui->tableTracks->setAlternatingRowColors(true);

    statusTimer.setInterval(100);

    connect(&statusTimer,
            &QTimer::timeout,
            this,
            [this]()
            {
                updateRadarStatus();
                updateTrackTable();
                updateTrackInspector();
            });

    statusTimer.start();

    updateRadarStatus();
    updateTrackTable();
    updateTrackInspector();

    connect(ui->tableTracks,
            &QTableWidget::cellClicked,
            this,
            &MainWindow::selectTrackFromTable);

    connect(radarWidget,
            &RadarWidget::targetSelected,
            this,
            &MainWindow::selectTrackFromRadar);

    connect(ui->btnStartScan,
            &QPushButton::clicked,
            server,
            &RadarServer::startScan);

    connect(ui->btnStopScan,
            &QPushButton::clicked,
            server,
            &RadarServer::stopScan);

    connect(ui->spinScanSpeed,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            server,
            &RadarServer::setScanSpeed);

    connect(ui->spinBeamWidth,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            server,
            &RadarServer::setBeamWidth);

    connect(ui->spinMaxRange,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            server,
            &RadarServer::setMaxDetectionRange);

    QVBoxLayout *radarLayout =
        new QVBoxLayout(ui->radarContainer);

    radarLayout->setContentsMargins(0, 0, 0, 0);

    radarLayout->addWidget(radarWidget);

    connect(server,
            &RadarServer::trackUpdated,
            this,
            [this]()
            {
                updateTrackTable();
            });
}

void MainWindow::updateRadarStatus()
{
    // Radar status
    if (server->isRadarRunning())
    {
        ui->lblRadarState->setText("Radar: RUNNING");

        ui->lblRadarIndicator->setStyleSheet(
            "color: green;"
            );
    }
    else
    {
        ui->lblRadarState->setText("Radar: STOPPED");

        ui->lblRadarIndicator->setStyleSheet(
            "color: red;"
            );
    }

    // Scan status
    if (server->isScanning())
    {
        ui->lblScanState->setText("Scan: SCANNING");

        ui->lblScanIndicator->setStyleSheet(
            "color: green;"
            );
    }
    else
    {
        ui->lblScanState->setText("Scan: IDLE");

        ui->lblScanIndicator->setStyleSheet(
            "color: gray;"
            );
    }

    ui->lblTargets->setText(
        QString("Targets: %1")
            .arg(server->getDetectedTargetIds().size())
        );

    ui->lblTracks->setText(
        QString("Tracks: %1")
            .arg(server->getTracks().size())
        );

    ui->lblCurrentAzimuth->setText(
        QString("Current Azimuth: %1°")
            .arg(server->getScanAngle(), 0, 'f', 1)
        );
}

void MainWindow::updateTrackTable()
{
    const QList<RadarTrack>& tracks =
        server->getTracks();

    ui->tableTracks->setRowCount(tracks.size());

    for (int row = 0; row < tracks.size(); ++row)
    {
        const RadarTrack& track = tracks[row];

        // ID
        QTableWidgetItem *idItem =
            new QTableWidgetItem(
                QString("T%1").arg(track.id)
                );

        idItem->setTextAlignment(Qt::AlignCenter);

        ui->tableTracks->setItem(row, 0, idItem);

        // HITS
        QTableWidgetItem *hitsItem =
            new QTableWidgetItem(
                QString::number(track.hitCount)
                );

        hitsItem->setTextAlignment(Qt::AlignCenter);

        ui->tableTracks->setItem(row, 5, hitsItem);

        // HISTORY
        QTableWidgetItem *historyItem =
            new QTableWidgetItem(
                QString::number(track.history.size())
                );

        historyItem->setTextAlignment(Qt::AlignCenter);

        ui->tableTracks->setItem(row, 6, historyItem);

        // STATE
        QString stateText;
        QColor stateColor;

        if (track.state == TrackState::Tracking)
        {
            stateText = "TRACKING";
            stateColor = QColor(0, 180, 0);
        }
        else
        {
            stateText = "LOST";
            stateColor = QColor(130, 130, 130);
        }

        QTableWidgetItem *stateItem =
            new QTableWidgetItem(stateText);

        stateItem->setTextAlignment(Qt::AlignCenter);
        stateItem->setForeground(QBrush(stateColor));

        ui->tableTracks->setItem(row, 1, stateItem);

        // AZIMUTH
        QTableWidgetItem *angleItem =
            new QTableWidgetItem(
                QString("%1°")
                    .arg(track.angle, 0, 'f', 1)
                );

        angleItem->setTextAlignment(Qt::AlignCenter);

        ui->tableTracks->setItem(row, 2, angleItem);

        // RANGE
        QTableWidgetItem *rangeItem =
            new QTableWidgetItem(
                QString("%1 m")
                    .arg(track.distance, 0, 'f', 1)
                );

        rangeItem->setTextAlignment(Qt::AlignCenter);

        ui->tableTracks->setItem(row, 3, rangeItem);

        // SPEED
        QTableWidgetItem *speedItem =
            new QTableWidgetItem(
                QString("%1 m/s")
                    .arg(track.estimatedSpeed, 0, 'f', 1)
                );

        speedItem->setTextAlignment(Qt::AlignCenter);

        ui->tableTracks->setItem(row, 4, speedItem);
    }
}

void MainWindow::updateTrackInspector()
{
    QList<QTableWidgetItem*> selectedItems =
        ui->tableTracks->selectedItems();

    if (selectedItems.isEmpty())
    {
        ui->lblInspectorTarget->setText("Target: -");
        ui->lblInspectorState->setText("State: -");
        ui->lblInspectorDetection->setText("Detection: -");
        ui->lblInspectorAzimuth->setText("Azimuth: -");
        ui->lblInspectorRange->setText("Range: -");
        ui->lblInspectorSpeed->setText("Speed: -");
        ui->lblInspectorTurnRate->setText("Turn Rate: -");
        ui->lblInspectorPredAzimuth->setText("Pred Azimuth: -");
        ui->lblInspectorHits->setText("Hits: -");
        ui->lblInspectorHistory->setText("History: -");
        ui->lblInspectorLastUpdate->setText("Last Update: -");

        return;
    }

    int row = selectedItems.first()->row();

    const QList<RadarTrack>& tracks =
        server->getTracks();

    if (row < 0 || row >= tracks.size())
        return;

    const RadarTrack& track = tracks[row];

    QString stateText;

    if (track.state == TrackState::Tracking)
        stateText = "TRACKING";
    else
        stateText = "LOST";

    bool detected = false;

    const QList<int>& detectedIds =
        server->getDetectedTargetIds();

    if (detectedIds.contains(track.id))
    {
        detected = true;
    }

    ui->lblInspectorTarget->setText(
        QString("Target: T%1").arg(track.id)
        );

    ui->lblInspectorState->setText(
        QString("State: %1").arg(stateText)
        );

    ui->lblInspectorDetection->setText(
        QString("Detection: %1")
            .arg(detected ? "YES" : "NO")
        );

    ui->lblInspectorAzimuth->setText(
        QString("Azimuth: %1°")
            .arg(track.angle, 0, 'f', 1)
        );

    ui->lblInspectorRange->setText(
        QString("Range: %1 m → %2 m")
            .arg(track.distance, 0, 'f', 1)
            .arg(track.predictedDistance, 0, 'f', 1)
        );

    ui->lblInspectorSpeed->setText(
        QString("Speed: %1 m/s")
            .arg(track.estimatedSpeed, 0, 'f', 1)
        );

    ui->lblInspectorTurnRate->setText(
        QString("Turn Rate: %1°/s")
            .arg(track.estimatedAngularSpeed, 0, 'f', 2)
        );

    ui->lblInspectorPredAzimuth->setText(
        QString("Pred Azimuth: %1°")
            .arg(track.predictedAngle, 0, 'f', 1)
        );

    ui->lblInspectorHits->setText(
        QString("Hits: %1")
            .arg(track.hitCount)
        );

    ui->lblInspectorHistory->setText(
        QString("History: %1")
            .arg(track.history.size())
        );

    QString timeText =
        QDateTime::fromMSecsSinceEpoch(
            track.lastUpdateTime
            ).toString("HH:mm:ss.zzz");

    ui->lblInspectorLastUpdate->setText(
        QString("Last Update: %1")
            .arg(timeText)
        );
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::selectTrackFromTable(int row)
{
    if (row < 0)
        return;

    const QList<RadarTrack>& tracks =
        server->getTracks();

    if (row >= tracks.size())
        return;

    int targetId = tracks[row].id;

    radarWidget->setSelectedTargetId(targetId);

    updateTrackInspector();
}

void MainWindow::selectTrackFromRadar(int targetId)
{
    const QList<RadarTrack>& tracks =
        server->getTracks();

    for (int row = 0; row < tracks.size(); ++row)
    {
        if (tracks[row].id == targetId)
        {
            ui->tableTracks->selectRow(row);
            updateTrackInspector();
            return;
        }
    }
}