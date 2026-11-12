#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "RadarServer.h"
#include "RadarWidget.h"
#include "Recorder.h"
#include "ReplayController.h"

#include <algorithm>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QSlider>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QHeaderView>
#include <QBrush>
#include <QColor>
#include <QAbstractItemView>
#include <QDateTime>
#include <QSignalBlocker>

MainWindow::MainWindow(RadarServer *server,
                       QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , server(server)
    , radarWidget(new RadarWidget)
    , statusTimer(this)
    , recorder(new Recorder(*server, this))
    , replay(new ReplayController(this))
{
    Q_ASSERT(server->thread() == thread());
    ui->setupUi(this);

    auto* controlsScroll = new QScrollArea(ui->centralwidget);
    controlsScroll->setWidgetResizable(true);
    controlsScroll->setFrameShape(QFrame::NoFrame);
    delete ui->horizontalLayout->replaceWidget(ui->controlsPanel, controlsScroll);
    controlsScroll->setWidget(ui->controlsPanel);
    ui->verticalLayout->setSpacing(3);
    ui->horizontalLayout->setStretch(0, 13);
    ui->horizontalLayout->setStretch(1, 7);

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

    statusTimer.setInterval(50);

    connect(&statusTimer,
            &QTimer::timeout,
            this,
            &MainWindow::refreshDisplay);

    statusTimer.start();

    connect(ui->tableTracks,
            &QTableWidget::itemSelectionChanged,
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

    setupRecordingControls();
    refreshDisplay();
}

void MainWindow::refreshDisplay()
{
    if (displayMode == DisplayMode::Live)
        displayFrame(server->snapshot());
    updateReplayTime();
}

void MainWindow::displayFrame(const RadarFrame& frame)
{
    displayedFrame = frame;
    radarWidget->setFrame(displayedFrame);
    updateRadarStatus();
    {
        const QSignalBlocker blocker(ui->tableTracks);
        updateTrackTable();
        selectTrackFromRadar(selectedTargetId);
    }
}

void MainWindow::setupRecordingControls()
{
    auto* dock = new QDockWidget(tr("Recording and replay"), this);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);
    auto* controls = new QHBoxLayout;
    auto* transport = new QHBoxLayout;
    auto* seekRow = new QHBoxLayout;
    layout->addLayout(controls);
    layout->addLayout(transport);
    layout->addLayout(seekRow);

    const auto button = [panel](QHBoxLayout* row, const char* name, const QString& text) {
        auto* control = new QPushButton(text, panel);
        control->setObjectName(QLatin1String(name));
        row->addWidget(control);
        return control;
    };
    recordButton = button(controls, "recordButton", tr("Start recording"));
    saveButton = button(controls, "saveButton", tr("Stop recording"));
    openButton = button(controls, "openButton", tr("Open recording"));
    liveButton = button(controls, "liveButton", tr("Return to live"));
    operationState = new QLabel(panel);
    operationState->setObjectName(QStringLiteral("operationState"));
    controls->addWidget(operationState, 1);

    playButton = button(transport, "playButton", tr("Play"));
    pauseButton = button(transport, "pauseButton", tr("Pause"));
    resumeButton = button(transport, "resumeButton", tr("Resume"));
    stopButton = button(transport, "stopButton", tr("Stop"));
    stepButton = button(transport, "stepButton", tr("Step forward"));
    speedBox = new QComboBox(panel);
    speedBox->setObjectName(QStringLiteral("speedBox"));
    speedBox->setAccessibleName(tr("Playback speed"));
    for (const int speed : {1, 2, 4})
        speedBox->addItem(tr("%1x").arg(speed), speed);
    transport->addWidget(speedBox);
    transport->addStretch();

    timeline = new QSlider(Qt::Horizontal, panel);
    timeline->setObjectName(QStringLiteral("timeline"));
    timeline->setAccessibleName(tr("Playback position"));
    timeline->setRange(0, 0);
    replayTime = new QLabel(panel);
    replayTime->setObjectName(QStringLiteral("replayTime"));
    seekRow->addWidget(timeline, 1);
    seekRow->addWidget(replayTime);
    dock->setWidget(panel);
    addDockWidget(Qt::BottomDockWidgetArea, dock);

    connect(recordButton, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getSaveFileName(this, tr("Save radar recording"),
            QStringLiteral("recording.radar.jsonl"), tr("Radar recordings (*.radar.jsonl *.jsonl)"),
            nullptr, QFileDialog::DontConfirmOverwrite);
        if (!path.isEmpty())
            startRecording(path);
    });
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::stopRecording);
    connect(openButton, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Open radar recording"), {},
            tr("Radar recordings (*.radar.jsonl *.jsonl *.part);;All files (*)"));
        if (!path.isEmpty())
            openRecording(path);
    });
    connect(liveButton, &QPushButton::clicked, this, &MainWindow::returnToLive);
    connect(playButton, &QPushButton::clicked, replay, &ReplayController::play);
    connect(pauseButton, &QPushButton::clicked, replay, &ReplayController::pause);
    connect(resumeButton, &QPushButton::clicked, replay, &ReplayController::resume);
    connect(stopButton, &QPushButton::clicked, replay, &ReplayController::stop);
    connect(stepButton, &QPushButton::clicked, replay, &ReplayController::stepForward);
    connect(speedBox, &QComboBox::currentIndexChanged, this, [this] {
        replay->setSpeed(speedBox->currentData().toInt());
    });
    connect(timeline, &QSlider::valueChanged, this, [this](int value) {
        if (timeline->maximum() > 0)
            replay->seek(static_cast<qint64>(static_cast<long double>(value) *
                                            replay->duration() / timeline->maximum()));
    });

    connect(recorder, &Recorder::saved, this, [this](const QString& path, qint64 count) {
        recordingFinished(tr("Saved %1 frames to %2").arg(count).arg(path));
    });
    connect(recorder, &Recorder::failed, this,
            [this](const QString& reason, const QString& partial, qint64 count) {
        recordingFinished(partial.isEmpty() ? tr("Recording failed: %1").arg(reason)
            : tr("Recording failed: %1. Partial file (%2 frames): %3").arg(reason).arg(count).arg(partial));
    });
    connect(replay, &ReplayController::stateChanged, this, &MainWindow::updateRecordingControls);
    connect(replay, &ReplayController::frameChanged, this, [this](const RadarFrame& frame) {
        if (displayMode == DisplayMode::Live)
            selectedTargetId = -1;
        displayMode = DisplayMode::Replay;
        displayFrame(frame);
        updateRecordingControls();
        const auto recovery = replay->recoveryReason();
        if (!recovery.isEmpty())
            operationState->setToolTip(tr("Recovered recording: %1").arg(recovery));
    });
    connect(replay, &ReplayController::errorOccurred, this, [this](const QString& reason) {
        statusBar()->showMessage(tr("Replay failed: %1").arg(reason));
    });
    connect(replay, &ReplayController::shutdownFinished, this, [this] {
        QTimer::singleShot(0, this, [this] { close(); });
    });
    updateRecordingControls();
}

bool MainWindow::startRecording(const QString& path)
{
    if (displayMode != DisplayMode::Live || recordingState != RecordingState::Idle ||
        replay->isLoading() || closeAfterSave || !recorder->start(path))
        return false;
    statusBar()->clearMessage();
    recordingState = RecordingState::Recording;
    updateRecordingControls();
    return true;
}

void MainWindow::stopRecording()
{
    if (recordingState != RecordingState::Recording)
        return;
    recordingState = RecordingState::Saving;
    recorder->stop();
    updateRecordingControls();
}

void MainWindow::recordingFinished(const QString& message)
{
    recordingState = RecordingState::Idle;
    statusBar()->showMessage(message);
    updateRecordingControls();
    if (closeAfterSave)
        QTimer::singleShot(0, this, [this] { close(); });
}

bool MainWindow::openRecording(const QString& path)
{
    if (path.isEmpty() || recordingState != RecordingState::Idle || closeAfterSave)
        return false;
    statusBar()->clearMessage();
    operationState->setToolTip({});
    replay->open(path, path.endsWith(QStringLiteral(".part"), Qt::CaseInsensitive)
        ? RecordingReader::Mode::RecoverPrefix : RecordingReader::Mode::Strict);
    updateRecordingControls();
    return true;
}

void MainWindow::returnToLive()
{
    displayMode = DisplayMode::Live;
    selectedTargetId = -1;
    replay->close();
    statusBar()->clearMessage();
    operationState->setToolTip({});
    refreshDisplay();
    updateRecordingControls();
}

void MainWindow::updateRecordingControls()
{
    using State = ReplayController::State;
    const auto state = replay->state();
    const bool loading = replay->isLoading();
    const bool live = displayMode == DisplayMode::Live;
    const bool idle = recordingState == RecordingState::Idle;
    const bool ready = !live && !loading && state != State::Closed && state != State::Error;
    recordButton->setEnabled(live && idle && !loading && !closeAfterSave);
    saveButton->setEnabled(recordingState == RecordingState::Recording);
    openButton->setEnabled(idle && !loading && !closeAfterSave);
    liveButton->setEnabled(!live || loading || state == State::Error);
    playButton->setEnabled(ready && (state == State::Ready || state == State::Finished));
    pauseButton->setEnabled(ready && state == State::Playing);
    resumeButton->setEnabled(ready && state == State::Paused);
    stopButton->setEnabled(ready && state != State::Ready);
    stepButton->setEnabled(ready && (state == State::Ready || state == State::Paused));
    speedBox->setEnabled(ready);
    timeline->setEnabled(ready && replay->duration() > 0);

    ui->btnStartScan->setEnabled(live);
    ui->btnStopScan->setEnabled(live);
    ui->spinScanSpeed->setEnabled(live);
    ui->spinBeamWidth->setEnabled(live);
    ui->spinMaxRange->setEnabled(live);

    if (loading)
        operationState->setText(tr("Loading recording…"));
    else if (recordingState == RecordingState::Recording)
        operationState->setText(tr("Live · Recording"));
    else if (recordingState == RecordingState::Saving)
        operationState->setText(tr("Live · Saving…"));
    else if (state == State::Error)
        operationState->setText(tr("Replay failed"));
    else if (live)
        operationState->setText(tr("Live"));
    else
    {
        QString mode = tr("Replay · Ready");
        if (state == State::Playing)
            mode = tr("Replay · Playing");
        else if (state == State::Paused)
            mode = tr("Replay · Paused");
        else if (state == State::Finished)
            mode = tr("Replay · Finished");
        if (!replay->recoveryReason().isEmpty())
            mode += tr(" (recovered)");
        operationState->setText(mode + tr(" · TCP controls live radar"));
    }
    updateReplayTime();
}

void MainWindow::updateReplayTime()
{
    const auto format = [](qint64 microseconds) {
        const auto ms = microseconds / 1000;
        return QStringLiteral("%1:%2:%3.%4")
            .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
            .arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
            .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
            .arg(ms % 1000, 3, 10, QLatin1Char('0'));
    };
    const auto duration = replay->duration();
    const int scale = static_cast<int>(std::min<qint64>(duration, 1000000));
    const QSignalBlocker blocker(timeline);
    timeline->setRange(0, scale);
    qint64 current = replay->currentTime();
    if ((timeline->isSliderDown() || replay->isPositioning()) && scale > 0)
        current = static_cast<qint64>(static_cast<long double>(timeline->value()) * duration / scale);
    else
        timeline->setValue(duration == 0 ? 0 :
            static_cast<int>(static_cast<long double>(current) * scale / duration));
    replayTime->setText(tr("%1 / %2").arg(format(current), format(duration)));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (recordingState != RecordingState::Idle)
    {
        closeAfterSave = true;
        event->ignore();
        stopRecording();
        return;
    }
    if (replay->shutdown())
    {
        closeAfterSave = true;
        updateRecordingControls();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::updateRadarStatus()
{
    // Radar status
    if (displayedFrame.scan.running)
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
    if (displayedFrame.scan.scanning)
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
            .arg(displayedFrame.detectedTargetIds.size())
        );

    ui->lblTracks->setText(
        QString("Tracks: %1")
            .arg(displayedFrame.tracks.size())
        );

    ui->lblCurrentAzimuth->setText(
        QString("Current Azimuth: %1°")
            .arg(displayedFrame.scan.angleDegrees, 0, 'f', 1)
        );
}

void MainWindow::updateTrackTable()
{
    const QList<RadarTrack>& tracks =
        displayedFrame.tracks;

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
        displayedFrame.tracks;

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
        displayedFrame.detectedTargetIds;

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
    statusTimer.stop();
    hide();
    disconnect(ui->tableTracks, nullptr, this, nullptr);
    disconnect(radarWidget, nullptr, this, nullptr);
    disconnect(timeline, nullptr, this, nullptr);
    disconnect(speedBox, nullptr, this, nullptr);
    delete ui;
}

void MainWindow::selectTrackFromTable()
{
    const auto rows = ui->tableTracks->selectionModel()->selectedRows();
    selectedTargetId = rows.isEmpty() ? -1 : displayedFrame.tracks.at(rows.first().row()).id;
    radarWidget->setSelectedTargetId(selectedTargetId);
    updateTrackInspector();
}

void MainWindow::selectTrackFromRadar(int targetId)
{
    const QSignalBlocker blocker(ui->tableTracks);
    const auto& tracks = displayedFrame.tracks;

    for (int row = 0; row < tracks.size(); ++row)
    {
        if (tracks[row].id == targetId)
        {
            ui->tableTracks->selectRow(row);
            selectTrackFromTable();
            return;
        }
    }
    ui->tableTracks->clearSelection();
    selectTrackFromTable();
}
