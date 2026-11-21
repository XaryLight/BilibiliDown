#ifndef BILI_CODE_DOWNLOADER_TASK_RUNNER_H
#define BILI_CODE_DOWNLOADER_TASK_RUNNER_H

#include "model/Types.h"
#include "parser/AbstractBaseParser.h"
#include "parser/impl/Parsers.h"

#include <QObject>
#include <QString>
#include <memory>
#include <atomic>

namespace bili::downloader {

/**
 * @brief 单个下载任务的执行器 (运行于 worker 线程)
 *
 *  执行流程:
 *   1. 逐 clip 查询播放链接 (playurl API)
 *   2. 下载 m4s (DASH) 或 flv 分片
 *   3. ffmpeg 合并为中间 mp4 (合集模式) 或最终 mp4
 *   4. 合集模式: 所有 clip 完成后 concat 合并为单个 mp4
 *   5. 重命名为最终文件名, 按需清理中间文件
 */
class TaskRunner : public QObject {
    Q_OBJECT
public:
    explicit TaskRunner(model::DownloadTask task, QObject *parent = nullptr);
    ~TaskRunner() override;

    // 阻塞式执行 (由 worker 线程调用)
    void run();

    // 请求停止 (线程安全)
    void requestStop() { m_stop.store(true, std::memory_order_relaxed); }

    int taskId() const { return m_task.taskId; }

signals:
    void progress(int taskId, qint64 received, qint64 total, double speedBytesPerSec);
    // 单个分P的进度 (clipIndex 从 0 开始; 供展开视图显示每个分P独立进度)
    void clipProgress(int taskId, int clipIndex, qint64 received, qint64 total, double speedBytesPerSec);
    // 单个分P完成/失败 (doneCount 为已完成分P数; sha256 为该分P文件摘要)
    void clipDone(int taskId, int clipIndex, const QString &file, int doneCount, int totalCount,
                  const QString &sha256);
    void statusChanged(int taskId, model::TaskStatus status, const QString &text);
    void clipStarted(int taskId, int clipIndex, const QString &clipTitle);
    void clipFinished(int taskId, int clipIndex, const QString &file);
    void finished(int taskId, bool success, const QString &error);
    void log(int taskId, const QString &line);

private:
    // 下载并转换单个 clip, 成功返回输出文件路径
    QString processClip(const model::ClipInfo &clip, int clipIndex,
                        const QString &workDir, const QString &finalBaseName,
                        bool forMerge);

    // 全部 clip 的执行入口 (run 的内部实现)
    bool processClipAll(QString &error);

    // 合集合并: 将多个中间 mp4 合并为单个 mp4
    bool mergeClips(const QStringList &clipFiles, const QString &finalPath);

    // 文件命名 (清理非法字符)
    static QString sanitizeName(const QString &raw);
    static QString uniqueFilePath(const QString &dir, const QString &baseName, const QString &suffix);

    model::DownloadTask m_task;
    std::unique_ptr<parser::BvParser> m_linkQuerier; // 复用 getVideoLink 能力
    std::atomic<bool> m_stop{false};

    // 速度统计
    qint64 m_lastReceived = 0;
    qint64 m_lastSpeedTime = 0;
    double m_speed = 0.0;
};

} // namespace bili::downloader

#endif // BILI_CODE_DOWNLOADER_TASK_RUNNER_H
