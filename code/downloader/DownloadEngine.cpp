#include "DownloadEngine.h"
#include <api/Logger.h>

namespace bili::downloader {

using bili::api::Logger;

DownloadEngine &DownloadEngine::instance() {
    static DownloadEngine s_instance;
    return s_instance;
}

DownloadEngine::DownloadEngine(QObject *parent) : QObject(parent) {}

DownloadEngine::~DownloadEngine() {
    stopAll();
}

int DownloadEngine::addTask(const model::DownloadTask &task) {
    QMutexLocker locker(&m_mutex);
    int taskId = m_nextTaskId++;
    Entry entry;
    entry.task = task;
    entry.task.taskId = taskId;
    entry.task.status = model::TaskStatus::Queued;
    entry.task.statusText = model::taskStatusText(model::TaskStatus::Queued);
    m_entries.insert(taskId, entry);
    Logger::info(QString("添加下载任务 #%1: %2").arg(taskId).arg(task.displayName()));
    locker.unlock();
    emit taskAdded(taskId);
    return taskId;
}

void DownloadEngine::removeTask(int taskId) {
    QMutexLocker locker(&m_mutex);
    auto it = m_entries.find(taskId);
    if (it == m_entries.end())
        return;
    if (it->runner) {
        // 运行中: 请求停止, 线程结束后再移除
        it->stopRequested = true;
        it->removeAfterFinish = true;
        it->runner->requestStop();
        return;
    }
    m_entries.erase(it);
    locker.unlock();
    emit taskRemoved(taskId);
}

void DownloadEngine::startTask(int taskId) {
    QMutexLocker locker(&m_mutex);
    auto it = m_entries.find(taskId);
    if (it == m_entries.end())
        return;
    if (it->thread) {
        // 已在运行
        return;
    }
    it->stopRequested = false;
    it->task.status = model::TaskStatus::Queued;
    it->task.statusText = "队列中";
    locker.unlock();
    launchNext();
}

void DownloadEngine::stopTask(int taskId) {
    QMutexLocker locker(&m_mutex);
    auto it = m_entries.find(taskId);
    if (it == m_entries.end() || !it->runner)
        return;
    it->stopRequested = true;
    it->runner->requestStop();
    it->task.statusText = "正在停止...";
    locker.unlock();
    emit taskStatusChanged(taskId, model::TaskStatus::Stopped, "正在停止...");
}

void DownloadEngine::startAllQueued() {
    QMutexLocker locker(&m_mutex);
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (!it->thread && !it->stopRequested) {
            it->task.status = model::TaskStatus::Queued;
            it->task.statusText = "队列中";
        }
    }
    locker.unlock();
    launchNext();
}

void DownloadEngine::stopAll() {
    QMutexLocker locker(&m_mutex);
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->runner) {
            it->stopRequested = true;
            it->runner->requestStop();
        }
    }
}

void DownloadEngine::setMaxConcurrent(int n) {
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    QMutexLocker locker(&m_mutex);
    m_maxConcurrent = n;
    locker.unlock();
    launchNext();
}

model::DownloadTask DownloadEngine::taskSnapshot(int taskId) const {
    QMutexLocker locker(&m_mutex);
    auto it = m_entries.constFind(taskId);
    if (it != m_entries.constEnd())
        return it->task;
    return model::DownloadTask{};
}

QList<int> DownloadEngine::taskIds() const {
    QMutexLocker locker(&m_mutex);
    return m_entries.keys();
}

bool DownloadEngine::isRunning(int taskId) const {
    QMutexLocker locker(&m_mutex);
    auto it = m_entries.constFind(taskId);
    return it != m_entries.constEnd() && it->thread != nullptr;
}

void DownloadEngine::launchNext() {
    QMutexLocker locker(&m_mutex);
    // 收集待启动任务 (按添加顺序)
    QList<int> pending;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (!it->thread && !it->stopRequested &&
            (it->task.status == model::TaskStatus::Queued))
            pending << it.key();
    }
    while (m_runningIds.size() < m_maxConcurrent && !pending.isEmpty()) {
        int taskId = pending.takeFirst();
        locker.unlock();
        launchTask(taskId);
        locker.relock();
    }
}

void DownloadEngine::launchTask(int taskId) {
    QMutexLocker locker(&m_mutex);
    auto it = m_entries.find(taskId);
    if (it == m_entries.end())
        return;

    Entry &entry = it.value();
    entry.task.status = model::TaskStatus::Downloading;
    entry.task.statusText = "开始下载";

    QThread *thread = new QThread(this);
    TaskRunner *runner = new TaskRunner(entry.task);
    runner->moveToThread(thread);

    // 信号转发: runner (worker 线程) -> engine (主线程)
    connect(runner, &TaskRunner::progress, this,
            [this](int id, qint64 received, qint64 total, double speed) {
                {
                    QMutexLocker l(&m_mutex);
                    auto e = m_entries.find(id);
                    if (e != m_entries.end()) {
                        e->task.receivedBytes = received;
                        e->task.totalBytes = total;
                        e->task.speedBytesPerSec = speed;
                    }
                }
                emit taskProgress(id, received, total, speed);
            }, Qt::QueuedConnection);

    connect(runner, &TaskRunner::statusChanged, this,
            [this](int id, model::TaskStatus status, const QString &text) {
                {
                    QMutexLocker l(&m_mutex);
                    auto e = m_entries.find(id);
                    if (e != m_entries.end()) {
                        e->task.status = status;
                        e->task.statusText = text;
                    }
                }
                emit taskStatusChanged(id, status, text);
            }, Qt::QueuedConnection);

    connect(runner, &TaskRunner::clipStarted, this,
            [this](int id, int clipIndex, const QString &title) {
                {
                    QMutexLocker l(&m_mutex);
                    auto e = m_entries.find(id);
                    if (e != m_entries.end())
                        e->task.currentClipTitle = title;
                }
                emit taskClipStarted(id, clipIndex, title);
            }, Qt::QueuedConnection);

    connect(runner, &TaskRunner::finished, this,
            [this](int id, bool success, const QString &error) {
                onRunnerFinished(id, success, error);
            }, Qt::QueuedConnection);

    connect(runner, &TaskRunner::log, this, [this](int id, const QString &line) {
        Q_UNUSED(id)
        emit logLine(line);
    }, Qt::QueuedConnection);

    connect(thread, &QThread::started, runner, &TaskRunner::run);
    connect(runner, &TaskRunner::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, runner, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    entry.runner = runner;
    entry.thread = thread;
    m_runningIds.insert(taskId);

    thread->start();
    Logger::info(QString("任务 #%1 开始执行 (并发 %2/%3)").arg(taskId).arg(m_runningIds.size()).arg(m_maxConcurrent));
}

void DownloadEngine::onRunnerFinished(int taskId, bool success, const QString &error) {
    bool stillTracked = false;
    bool removeAfterFinish = false;
    {
        QMutexLocker locker(&m_mutex);
        m_runningIds.remove(taskId);
        auto it = m_entries.find(taskId);
        if (it != m_entries.end()) {
            stillTracked = true;
            bool stopped = it->stopRequested;
            it->task.status = stopped ? model::TaskStatus::Stopped
                                      : (success ? model::TaskStatus::Success
                                                 : model::TaskStatus::Failed);
            it->task.statusText = stopped ? "已停止"
                                          : (success ? "完成" : (error.isEmpty() ? "失败" : error));
            it->task.errorMessage = error;
            it->task.speedBytesPerSec = 0;
            it->runner = nullptr;
            it->thread = nullptr;
            removeAfterFinish = it->removeAfterFinish;
            if (removeAfterFinish)
                m_entries.erase(it);
        }
    }
    if (stillTracked && !removeAfterFinish) {
        emit taskFinished(taskId, success, error);
    } else if (!stillTracked || removeAfterFinish) {
        emit taskRemoved(taskId);
    }
    // 继续下一个排队任务
    launchNext();
}

} // namespace bili::downloader
