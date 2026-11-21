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
#include <QFont>

namespace bili::ui {

using bili::api::Config;
namespace model = bili::model;

// 角色: 区分顶层任务项 / 分P子项
static const int ROLE_TASK_ID = Qt::UserRole + 1;
static const int ROLE_CLIP_INDEX = Qt::UserRole + 2; // -1 表示顶层任务项

TabDownload::TabDownload(QWidget *parent) : QWidget(parent) {
    setupUi();

    auto &engine = bili::downloader::DownloadEngine::instance();
    connect(&engine, &bili::downloader::DownloadEngine::taskAdded, this, &TabDownload::onTaskAdded);
    connect(&engine, &bili::downloader::DownloadEngine::taskRemoved, this, &TabDownload::onTaskRemoved);
    connect(&engine, &bili::downloader::DownloadEngine::taskProgress, this, &TabDownload::onTaskProgress);
    connect(&engine, &bili::downloader::DownloadEngine::clipProgress, this, &TabDownload::onClipProgress);
    connect(&engine, &bili::downloader::DownloadEngine::clipDone, this, &TabDownload::onClipDone);
    connect(&engine, &bili::downloader::DownloadEngine::taskStatusChanged, this, &TabDownload::onTaskStatusChanged);
    connect(&engine, &bili::downloader::DownloadEngine::taskFinished, this, &TabDownload::onTaskFinished);

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &TabDownload::refreshSpeeds);
    m_refreshTimer->start(1000);
}

void TabDownload::setupUi() {
    QVBoxLayout *layout = new QVBoxLayout(this);

    // 工具按钮
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

    // 树形任务池
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(8);
    QStringList headers;
    headers << "ID/分P" << "标题" << "清晰度" << "状态" << "进度" << "分P" << "大小" << "速度";
    m_tree->setHeaderLabels(headers);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setAlternatingRowColors(true);
    m_tree->setIndentation(20);
    m_tree->setRootIsDecorated(true);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->header()->resizeSection(0, 64);
    m_tree->header()->resizeSection(4, 160);
    m_tree->header()->resizeSection(5, 64);
    layout->addWidget(m_tree, 1);
}

QTreeWidgetItem *TabDownload::topItemOf(int taskId) const {
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = m_tree->topLevelItem(i);
        if (it && it->data(0, ROLE_TASK_ID).toInt() == taskId &&
            it->data(0, ROLE_CLIP_INDEX).toInt() == -1)
            return it;
    }
    return nullptr;
}

QString TabDownload::formatSize(qint64 bytes) {
    if (bytes <= 0) return "-";
    if (bytes >= 1024LL * 1024 * 1024)
        return QString::number(bytes / 1024.0 / 1024.0 / 1024.0, 'f', 2) + " GB";
    if (bytes >= 1024 * 1024)
        return QString::number(bytes / 1024.0 / 1024.0, 'f', 2) + " MB";
    if (bytes >= 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    return QString::number(bytes) + " B";
}

QString TabDownload::formatSpeed(double bytesPerSec) {
    if (bytesPerSec <= 0) return "-";
    if (bytesPerSec >= 1024 * 1024)
        return QString::number(bytesPerSec / 1024.0 / 1024.0, 'f', 2) + " MB/s";
    if (bytesPerSec >= 1024)
        return QString::number(bytesPerSec / 1024.0, 'f', 1) + " KB/s";
    return QString::number(bytesPerSec, 'f', 0) + " B/s";
}

void TabDownload::onTaskAdded(int taskId) {
    auto task = bili::downloader::DownloadEngine::instance().taskSnapshot(taskId);
    auto *top = new QTreeWidgetItem();
    top->setData(0, ROLE_TASK_ID, taskId);
    top->setData(0, ROLE_CLIP_INDEX, -1);
    top->setText(0, QString("#%1").arg(taskId));
    top->setText(1, task.displayName());
    top->setText(2, model::videoQualityText(task.qn));
    top->setText(3, model::taskStatusText(task.status));
    top->setText(4, "0%");
    top->setText(5, QString("0/%1").arg(task.videoInfo.clipCount()));
    top->setText(6, "-");
    top->setText(7, "-");
    QFont bold = top->font(1);
    bold.setBold(true);
    top->setFont(1, bold);

    // 多分P: 建立子项 (可展开查看每个分P进度)
    const int clipCount = task.videoInfo.clipCount();
    for (int i = 0; i < clipCount; ++i) {
        const auto &clip = task.videoInfo.clips.at(i);
        auto *child = new QTreeWidgetItem();
        child->setData(0, ROLE_TASK_ID, taskId);
        child->setData(0, ROLE_CLIP_INDEX, i);
        child->setText(0, QString("p%1").arg(clip.page > 0 ? clip.page : i + 1));
        child->setText(1, clip.title.isEmpty() ? clip.avTitle : clip.title);
        child->setText(3, "等待");
        child->setText(4, "0%");
        top->addChild(child);
    }
    // 单项视频不默认展开; 多分P默认展开便于查看
    top->setExpanded(clipCount > 1);
    m_tree->addTopLevelItem(top);
}

void TabDownload::onTaskRemoved(int taskId) {
    if (auto *top = topItemOf(taskId))
        delete top;
}

void TabDownload::updateTopProgress(int taskId) {
    auto *top = topItemOf(taskId);
    if (!top) return;
    auto task = bili::downloader::DownloadEngine::instance().taskSnapshot(taskId);
    // 顶层进度 = 各分P平均完成度; 分P列显示 已完成/总数
    double sum = 0.0;
    int counted = 0;
    for (const auto &cp : task.clipProgress) {
        if (cp.total > 0) { sum += double(cp.received) / double(cp.total); ++counted; }
    }
    double pct = counted > 0 ? sum / counted * 100.0 : task.progress() * 100.0;
    top->setText(4, QString("%1%").arg(pct, 0, 'f', 1));
    top->setText(5, QString("%1/%2").arg(task.doneClips).arg(task.totalClips));
}

void TabDownload::onTaskProgress(int taskId, qint64 received, qint64 total, double speed) {
    auto *top = topItemOf(taskId);
    if (!top) return;
    top->setText(6, formatSize(total));
    if (speed > 0) top->setText(7, formatSpeed(speed));
    updateTopProgress(taskId);
}

void TabDownload::onClipProgress(int taskId, int clipIndex, qint64 received, qint64 total, double speed) {
    auto *top = topItemOf(taskId);
    if (!top) return;
    auto *child = top->child(clipIndex);
    if (!child) return;
    double pct = total > 0 ? double(received) / double(total) * 100.0 : 0.0;
    child->setText(4, QString("%1%  %2/%3").arg(pct, 0, 'f', 1)
                          .arg(formatSize(received), formatSize(total)));
    child->setText(6, formatSize(total));
    child->setText(7, formatSpeed(speed));
    if (child->text(3) == "等待") child->setText(3, "下载中");
    updateTopProgress(taskId);
}

void TabDownload::onClipDone(int taskId, int clipIndex, const QString &file,
                             int doneCount, int totalCount, const QString &sha256) {
    auto *top = topItemOf(taskId);
    if (!top) return;
    auto *child = top->child(clipIndex);
    if (child) {
        child->setText(3, "完成");
        child->setText(4, "100%");
        child->setText(7, "-");
        child->setText(1, QString("%1  [SHA256 %2]")
                              .arg(child->text(1), sha256.isEmpty() ? "?" : sha256.left(12)));
        child->setToolTip(1, file + "\nSHA256: " + sha256);
        child->setForeground(3, QBrush(QColor(0, 150, 0)));
    }
    top->setText(5, QString("%1/%2").arg(doneCount).arg(totalCount));
    updateTopProgress(taskId);
}

void TabDownload::onTaskStatusChanged(int taskId, model::TaskStatus status, const QString &text) {
    auto *top = topItemOf(taskId);
    if (!top) return;
    QString statusStr = model::taskStatusText(status);
    if (!text.isEmpty()) statusStr += " - " + text;
    top->setText(3, statusStr);
    QColor color;
    switch (status) {
        case model::TaskStatus::Success: color = QColor(0, 150, 0); break;
        case model::TaskStatus::Failed:  color = QColor(200, 0, 0); break;
        case model::TaskStatus::Stopped: color = QColor(150, 150, 0); break;
        case model::TaskStatus::Merging: color = QColor(0, 100, 200); break;
        default: color = QColor(60, 60, 60); break;
    }
    top->setForeground(3, QBrush(color));
}

void TabDownload::onTaskFinished(int taskId, bool success, const QString &error) {
    auto *top = topItemOf(taskId);
    if (!top) return;
    top->setText(7, "-");
    if (!success && !error.isEmpty()) {
        top->setText(3, "失败 - " + error);
        top->setForeground(3, QBrush(QColor(200, 0, 0)));
    }
    if (success) {
        auto task = bili::downloader::DownloadEngine::instance().taskSnapshot(taskId);
        QString output = task.outputFile.isEmpty() && !task.outputFiles.isEmpty()
                             ? task.outputFiles.last() : task.outputFile;
        if (!output.isEmpty()) {
            top->setText(1, task.displayName() + "  ->  " + QDir::toNativeSeparators(output));
            top->setToolTip(1, output);
        }
    }
    updateTopProgress(taskId);
}

// 取选中行的顶层任务 id
static QList<int> selectedTaskIds(QTreeWidget *tree) {
    QList<int> ids;
    for (QTreeWidgetItem *it : tree->selectedItems()) {
        if (!it) continue;
        int id = it->data(0, ROLE_TASK_ID).toInt();
        if (id > 0 && !ids.contains(id)) ids << id;
    }
    return ids;
}

void TabDownload::onStartClicked() {
    auto ids = selectedTaskIds(m_tree);
    if (ids.isEmpty()) { QMessageBox::information(this, "提示", "请先选择任务"); return; }
    for (int id : ids) bili::downloader::DownloadEngine::instance().startTask(id);
}

void TabDownload::onStopClicked() {
    auto ids = selectedTaskIds(m_tree);
    if (ids.isEmpty()) { QMessageBox::information(this, "提示", "请先选择任务"); return; }
    for (int id : ids) bili::downloader::DownloadEngine::instance().stopTask(id);
}

void TabDownload::onRemoveClicked() {
    auto ids = selectedTaskIds(m_tree);
    if (ids.isEmpty()) { QMessageBox::information(this, "提示", "请先选择任务"); return; }
    int taskId = ids.first();
    auto &engine = bili::downloader::DownloadEngine::instance();
    if (engine.isRunning(taskId)) {
        auto ret = QMessageBox::question(this, "确认", QString("任务 #%1 正在下载, 确定要移除吗?").arg(taskId));
        if (ret != QMessageBox::Yes) return;
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
    auto &engine = bili::downloader::DownloadEngine::instance();
    for (int taskId : engine.taskIds()) {
        updateTopProgress(taskId);
        auto task = engine.taskSnapshot(taskId);
        auto *top = topItemOf(taskId);
        if (top && task.status != model::TaskStatus::Downloading)
            top->setText(7, "-");
    }
}

} // namespace bili::ui
