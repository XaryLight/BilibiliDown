#ifndef BILI_CODE_DOWNLOADER_DOWNLOAD_ENGINE_H
#define BILI_CODE_DOWNLOADER_DOWNLOAD_ENGINE_H

#include "model/Types.h"
#include "TaskRunner.h"

#include <QObject>
#include <QThread>
#include <QMap>
#include <QMutex>
#include <QSet>

namespace bili::downloader {

/**
 * @brief 下载引擎 (单例)
 *
 *  - 管理下载任务队列与并发数 (默认 2 个任务并行)
 *  - 每个任务运行于独立 QThread
 *  - 通过信号向 GUI 汇报进度/状态
 */
class DownloadEngine : public QObject {
    Q_OBJECT
public:
    static DownloadEngine &instance();

    // 添加任务 (返回 taskId)
    int addTask(const model::DownloadTask &task);

    // 移除任务 (停止中的任务会先请求停止)
    void removeTask(int taskId);

    // 启动 / 停止 / 重试
    void startTask(int taskId);
    void stopTask(int taskId);
    void startAllQueued();
    void stopAll();

    // 并发设置
    int maxConcurrent() const { return m_maxConcurrent; }
    void setMaxConcurrent(int n);

    // 任务快照 (供 GUI 显示)
    model::DownloadTask taskSnapshot(int taskId) const;
    QList<int> taskIds() const;

    bool isRunning(int taskId) const;

signals:
    void taskAdded(int taskId);
    void taskRemoved(int taskId);
    void taskProgress(int taskId, qint64 received, qint64 total, double speed);
    void taskStatusChanged(int taskId, model::TaskStatus status, const QString &text);
    void taskClipStarted(int taskId, int clipIndex, const QString &clipTitle);
    void taskFinished(int taskId, bool success, const QString &error);
    void logLine(const QString &line);

private:
    explicit DownloadEngine(QObject *parent = nullptr);
    ~DownloadEngine() override;

    struct Entry {
        model::DownloadTask task;
        TaskRunner *runner = nullptr;
        QThread *thread = nullptr;
        bool stopRequested = false;
        bool removeAfterFinish = false;
    };

    void launchNext();
    void launchTask(int taskId);
    void onRunnerFinished(int taskId, bool success, const QString &error);

    mutable QMutex m_mutex;
    QMap<int, Entry> m_entries;
    QSet<int> m_runningIds;
    int m_nextTaskId = 1;
    int m_maxConcurrent = 2;
};

} // namespace bili::downloader

#endif // BILI_CODE_DOWNLOADER_DOWNLOAD_ENGINE_H
