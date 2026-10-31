#include "TabDownload.h"

#include <api/Logger.h>
#include <api/Config.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QPushButton>
#include <QColor>
#include <QBrush>

namespace bili::ui {

using bili::api::Config;
namespace model = bili::model;

TabDownload::TabDownload(QWidget *parent) : QWidget(parent) {
    setupUi();

    // 引擎信号连接
    auto &engine = bili::downloader::DownloadEngine::instance();
    connect(&engine, &bili::downloader::DownloadEngine::taskAdded,
            this, &TabDownload::onTaskAdded);
    connect(&engine, &bili::downloader::DownloadEngine::taskRemoved,
            this, &TabDownload::onTaskRemoved);
    connect(&engine, &bili::downloader::DownloadEngine::taskProgress,
            this, &TabDownload::onTaskProgress);
    connect(&engine, &bili::downloader::DownloadEngine::taskStatusChanged,
            this, &TabDownload::onTaskStatusChanged);
    connect(&engine, &bili::downloader::DownloadEngine::taskFinished,
            this, &TabDownload::onTaskFinished);
    connect(&engine, &bili::downloader::DownloadEngine::logLine,
            this, [](const QString &line) { Q_UNUSED(line) });

    // 速度定时刷新
    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &TabDownload::refreshSpeeds);
    m_refreshTimer->start(1000);
}

void TabDownload::setupUi() {
    QVBoxLayout *layout = new QVBoxLayout(this);

    // ---- 工具按钮 ----
    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_startBtn = new QPushButton("开始", this);
    m_stopBtn = new QPushButton("停止", this);
    m_removeBtn = new QPushButton("移除", this);
    m_startAllBtn = new QPushButton("全部开始", this);
    m_stopAllBtn = new QPushButton("全部停止", this);
    QPushButton *openFolderBtn = new QPushButton("打开下载目录", this);
    btnLayout->addWidget(m_startBtn);
    btnLayout->addWidget(m_stopBtn);
    btnLayout->addWidget(m_removeBtn);
    btnLayout->addWidget(m_startAllBtn);
    btnLayout->addWidget(m_stopAllBtn);
    btnLayout->addStretch(1);
    btnLayout->addWidget(openFolderBtn);
    layout->addLayout(btnLayout);

    connect(m_startBtn, &QPushButton::clicked, this, &TabDownload::onStartClicked);
    connect(m_stopBtn, &QPushButton::clicked, this, &TabDownload::onStopClicked);
    connect(m_removeBtn, &QPushButton::clicked, this, &TabDownload::onRemoveClicked);
    connect(m_startAllBtn, &QPushButton::clicked, this, &TabDownload::onStartAllClicked);
    connect(m_stopAllBtn, &QPushButton::clicked, this, &TabDownload::onStopAllClicked);
    connect(openFolderBtn, &QPushButton::clicked, this, &TabDownload::onOpenFolderClicked);

    // ---- 任务表格 ----
    m_model = new QStandardItemModel(this);
    m_model->setColumnCount(8);
    m_model->setHeaderData(0, Qt::Horizontal, "ID");
    m_model->setHeaderData(1, Qt::Horizontal, "标题");
    m_model->setHeaderData(2, Qt::Horizontal, "清晰度");
    m_model->setHeaderData(3, Qt::Horizontal, "状态");
    m_model->setHeaderData(4, Qt::Horizontal, "进度");
    m_model->setHeaderData(5, Qt::Horizontal, "分P");
    m_model->setHeaderData(6, Qt::Horizontal, "大小");
    m_model->setHeaderData(7, Qt::Horizontal, "速度");

    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->resizeSection(0, 40);
    m_table->horizontalHeader()->resizeSection(4, 150);
    layout->addWidget(m_table, 1);
}

int TabDownload::rowOfTask(int taskId) const {
    for (int row = 0; row < m_model->rowCount(); row++) {
        if (m_model->item(row, 0) && m_model->item(row, 0)->text().toInt() == taskId)
            return row;
    }
    return -1;
}

QString TabDownload::formatSize(qint64 bytes) const {
    if (bytes <= 0) return "-";
    if (bytes >= 1024LL * 1024 * 1024)
        return QString::number(bytes / 1024.0 / 1024.0 / 1024.0, 'f', 2) + " GB";
    if (bytes >= 1024 * 1024)
        return QString::number(bytes / 1024.0 / 1024.0, 'f', 2) + " MB";
    if (bytes >= 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    return QString::number(bytes) + " B";
}

QString TabDownload::formatSpeed(double bytesPerSec) const {
    if (bytesPerSec <= 0) return "-";
    if (bytesPerSec >= 1024 * 1024)
        return QString::number(bytesPerSec / 1024.0 / 1024.0, 'f', 2) + " MB/s";
    if (bytesPerSec >= 1024)
        return QString::number(bytesPerSec / 1024.0, 'f', 1) + " KB/s";
    return QString::number(bytesPerSec, 'f', 0) + " B/s";
}

void TabDownload::onTaskAdded(int taskId) {
    auto task = bili::downloader::DownloadEngine::instance().taskSnapshot(taskId);
    QList<QStandardItem *> row;
    row << new QStandardItem(QString::number(taskId));
    row << new QStandardItem(task.displayName());
    row << new QStandardItem(model::videoQualityText(task.qn));
    row << new QStandardItem(model::taskStatusText(task.status));
    row << new QStandardItem("0%");
    row << new QStandardItem(QString("0/%1").arg(task.videoInfo.clipCount()));
    row << new QStandardItem("-");
    row << new QStandardItem("-");
    for (auto *item : row)
        item->setEditable(false);
    m_model->appendRow(row);
}

void TabDownload::onTaskRemoved(int taskId) {
    int row = rowOfTask(taskId);
    if (row >= 0)
        m_model->removeRow(row);
}

void TabDownload::onTaskProgress(int taskId, qint64 received, qint64 total, double speed) {
    int row = rowOfTask(taskId);
    if (row < 0) return;
    double pct = total > 0 ? static_cast<double>(received) / static_cast<double>(total) * 100.0 : 0.0;
    // 进度条文本
    QString progressText = QString("%1%  %2/%3")
                               .arg(pct, 0, 'f', 1)
                               .arg(formatSize(received), formatSize(total));
    m_model->item(row, 4)->setText(progressText);
    m_model->item(row, 6)->setText(formatSize(total));
    if (speed > 0)
        m_model->item(row, 7)->setText(formatSpeed(speed));
}

void TabDownload::onTaskStatusChanged(int taskId, model::TaskStatus status, const QString &text) {
    int row = rowOfTask(taskId);
    if (row < 0) return;
    QString statusStr = model::taskStatusText(status);
    if (!text.isEmpty())
        statusStr += " - " + text;
    m_model->item(row, 3)->setText(statusStr);

    // 颜色提示
    QColor color;
    switch (status) {
        case model::TaskStatus::Success: color = QColor(0, 150, 0); break;
        case model::TaskStatus::Failed:  color = QColor(200, 0, 0); break;
        case model::TaskStatus::Stopped: color = QColor(150, 150, 0); break;
        case model::TaskStatus::Merging: color = QColor(0, 100, 200); break;
        default: color = QColor(60, 60, 60); break;
    }
    m_model->item(row, 3)->setForeground(QBrush(color));
}

void TabDownload::onTaskFinished(int taskId, bool success, const QString &error) {
    int row = rowOfTask(taskId);
    if (row < 0) return;
    m_model->item(row, 7)->setText("-");
    if (!success && !error.isEmpty()) {
        m_model->item(row, 3)->setText("失败 - " + error);
        m_model->item(row, 3)->setForeground(QBrush(QColor(200, 0, 0)));
    }
    if (success) {
        auto task = bili::downloader::DownloadEngine::instance().taskSnapshot(taskId);
        QString output = task.outputFile.isEmpty() && !task.outputFiles.isEmpty()
                             ? task.outputFiles.last() : task.outputFile;
        if (!output.isEmpty()) {
            m_model->item(row, 1)->setText(task.displayName() + "  ->  " + QDir::toNativeSeparators(output));
            m_model->item(row, 1)->setToolTip(output);
        }
    }
}

void TabDownload::onStartClicked() {
    auto selection = m_table->selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        QMessageBox::information(this, "提示", "请先选择任务");
        return;
    }
    for (const QModelIndex &idx : selection) {
        int taskId = m_model->item(idx.row(), 0)->text().toInt();
        bili::downloader::DownloadEngine::instance().startTask(taskId);
    }
}

void TabDownload::onStopClicked() {
    auto selection = m_table->selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        QMessageBox::information(this, "提示", "请先选择任务");
        return;
    }
    for (const QModelIndex &idx : selection) {
        int taskId = m_model->item(idx.row(), 0)->text().toInt();
        bili::downloader::DownloadEngine::instance().stopTask(taskId);
    }
}

void TabDownload::onRemoveClicked() {
    auto selection = m_table->selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        QMessageBox::information(this, "提示", "请先选择任务");
        return;
    }
    int taskId = m_model->item(selection.first().row(), 0)->text().toInt();
    auto &engine = bili::downloader::DownloadEngine::instance();
    auto task = engine.taskSnapshot(taskId);
    if (engine.isRunning(taskId)) {
        auto ret = QMessageBox::question(this, "确认", QString("任务 #%1 正在下载, 确定要移除吗?").arg(taskId));
        if (ret != QMessageBox::Yes)
            return;
    }
    engine.removeTask(taskId);
}

void TabDownload::onStartAllClicked() {
    bili::downloader::DownloadEngine::instance().startAllQueued();
}

void TabDownload::onStopAllClicked() {
    bili::downloader::DownloadEngine::instance().stopAll();
}

void TabDownload::onOpenFolderClicked() {
    QString path = Config::instance().savePath();
    QDir().mkpath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void TabDownload::refreshSpeeds() {
    // 定时从引擎拉取最新任务状态刷新表格 (速度/分P进度)
    auto &engine = bili::downloader::DownloadEngine::instance();
    for (int taskId : engine.taskIds()) {
        int row = rowOfTask(taskId);
        if (row < 0) continue;
        auto task = engine.taskSnapshot(taskId);
        if (task.totalClips > 0)
            m_model->item(row, 5)->setText(
                QString("%1/%2").arg(task.doneClips).arg(task.totalClips));
        if (task.status == model::TaskStatus::Downloading && task.speedBytesPerSec > 0)
            m_model->item(row, 7)->setText(formatSpeed(task.speedBytesPerSec));
        else if (task.status != model::TaskStatus::Downloading)
            m_model->item(row, 7)->setText("-");
    }
}

} // namespace bili::ui
