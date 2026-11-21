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
    // 等待所有运行中的任务线程真正结束 (退出前调用, 避免进程残留)
    void waitAllStopped();
    // 是否有正在下载的任务 (关窗提示用)
    bool hasActiveTasks() const;

    // 并发设置
    int maxConcurrent() const { return m_maxConcurrent; }
    void setMaxConcurrent(int n);

    // 任务快照 (供 GUI 显示)
    model::DownloadTask taskSnapshot(int taskId) const;
    QList<int> taskIds() const;
    QList<model::DownloadTask> snapshotAll() const; // 全部任务快照 (持久化用)

    // 批量添加 (恢复用), 返回新任务 id 列表
    QList<int> restoreTasks(const QList<model::DownloadTask> &tasks);

    // 任务持久化: 保存到配置目录, 启动时恢复未完成任务
    void saveTasksToDisk();
    void loadTasksFromDisk();

    bool isRunning(int taskId) const;

signals:
    void taskAdded(int taskId);
    void taskRemoved(int taskId);
    void taskProgress(int taskId, qint64 received, qint64 total, double speed);
    // 单个分P进度/完成 (供 UI 展开显示)
    void clipProgress(int taskId, int clipIndex, qint64 received, qint64 total, double speed);
    void clipDone(int taskId, int clipIndex, const QString &file, int doneCount, int totalCount,
                  const QString &sha256);
    void taskStatusChanged(int taskId, model::TaskStatus status, const QString &text);
    void taskClipStarted(int taskId, int clipIndex, const QString &clipTitle);
    void taskFinished(int taskId, bool success, const QString &error);
    void logLine(const QString &line);

    // 引擎请求主窗口在退出前暂停并持久化任务 (关窗提示)
    void requestPauseAndPersist();

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
