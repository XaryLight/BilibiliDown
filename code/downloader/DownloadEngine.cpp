#include "DownloadEngine.h"
#include <api/Logger.h>
#include <api/Config.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>

namespace bili::downloader {

using bili::api::Logger;
using bili::api::Config;

// ---------------- 任务序列化 (持久化/恢复用) ----------------
static QJsonObject clipToJson(const bili::model::ClipInfo &c) {
    QJsonObject o;
    o["avTitle"] = c.avTitle;
    o["avId"] = c.avId;
    o["cid"] = QString::number(c.cid);
    o["page"] = c.page;
    o["title"] = c.title;
    o["pic"] = c.picPreview;
    o["listName"] = c.listName;
    o["listOwnerName"] = c.listOwnerName;
    o["listOwnerId"] = c.listOwnerId;
    o["upName"] = c.upName;
    o["upId"] = c.upId;
    o["favTime"] = QString::number(c.favTime);
    o["cTime"] = QString::number(c.cTime);
    o["remark"] = c.remark;
    return o;
}

static bili::model::ClipInfo clipFromJson(const QJsonObject &o) {
    bili::model::ClipInfo c;
    c.avTitle = o.value("avTitle").toString();
    c.avId = o.value("avId").toString();
    c.cid = o.value("cid").toString().toLongLong();
    c.page = o.value("page").toInt(1);
    c.title = o.value("title").toString();
    c.picPreview = o.value("pic").toString();
    c.listName = o.value("listName").toString();
    c.listOwnerName = o.value("listOwnerName").toString();
    c.listOwnerId = o.value("listOwnerId").toString();
    c.upName = o.value("upName").toString();
    c.upId = o.value("upId").toString();
    c.favTime = o.value("favTime").toString().toLongLong();
    c.cTime = o.value("cTime").toString().toLongLong();
    c.remark = o.value("remark").toInt(-1);
    return c;
}

static QJsonObject taskToJson(const bili::model::DownloadTask &t) {
    QJsonObject o;
    o["inputUrl"] = t.inputUrl;
    o["qn"] = t.qn;
    o["format"] = t.format;
    o["mergeCollection"] = t.mergeCollection;
    o["deleteTempAfterMerge"] = t.deleteTempAfterMerge;
    QJsonObject vi;
    vi["videoName"] = t.videoInfo.videoName;
    vi["videoId"] = t.videoInfo.videoId;
    vi["author"] = t.videoInfo.author;
    vi["authorId"] = t.videoInfo.authorId;
    vi["videoPreview"] = t.videoInfo.videoPreview;
    vi["brief"] = t.videoInfo.brief;
    vi["videoLink"] = t.videoInfo.videoLink;
    vi["isCollection"] = t.videoInfo.isCollection;
    QJsonArray clips;
    for (const auto &c : t.videoInfo.clips)
        clips.append(clipToJson(c));
    vi["clips"] = clips;
    o["videoInfo"] = vi;
    // 已完成分P数 (恢复时用于展示进度; 实际文件靠 sha256/存在性校验决定跳过)
    o["doneClips"] = t.doneClips;
    return o;
}

static bili::model::DownloadTask taskFromJson(const QJsonObject &o) {
    bili::model::DownloadTask t;
    t.inputUrl = o.value("inputUrl").toString();
    t.qn = o.value("qn").toInt(80);
    t.format = o.value("format").toInt(0);
    t.mergeCollection = o.value("mergeCollection").toBool(false);
    t.deleteTempAfterMerge = o.value("deleteTempAfterMerge").toBool(true);
    QJsonObject vi = o.value("videoInfo").toObject();
    t.videoInfo.videoName = vi.value("videoName").toString();
    t.videoInfo.videoId = vi.value("videoId").toString();
    t.videoInfo.author = vi.value("author").toString();
    t.videoInfo.authorId = vi.value("authorId").toString();
    t.videoInfo.videoPreview = vi.value("videoPreview").toString();
    t.videoInfo.brief = vi.value("brief").toString();
    t.videoInfo.videoLink = vi.value("videoLink").toString();
    t.videoInfo.isCollection = vi.value("isCollection").toBool(false);
    for (const QJsonValue &v : vi.value("clips").toArray())
        t.videoInfo.clips.append(clipFromJson(v.toObject()));
    t.doneClips = o.value("doneClips").toInt(0);
    return t;
}

static QString tasksFilePath() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) dir = QCoreApplication::applicationDirPath();
    QDir().mkpath(dir);
    return dir + "/tasks.json";
}

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
    entry.task.totalClips = task.videoInfo.clipCount();
    // 初始化每个分P进度槽位 (参与任务池展开显示)
    entry.task.clipProgress.clear();
    for (int i = 0; i < entry.task.totalClips; ++i)
        entry.task.clipProgress.append(model::DownloadTask::ClipProgress{});
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

bool DownloadEngine::hasActiveTasks() const {
    QMutexLocker locker(&m_mutex);
    return !m_runningIds.isEmpty();
}

void DownloadEngine::waitAllStopped() {
    QList<QThread *> threads;
    {
        QMutexLocker locker(&m_mutex);
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
            if (it->thread) threads << it->thread;
    }
    for (QThread *t : threads) {
        if (t->isRunning()) {
            t->quit();
            t->wait(5000);
            if (t->isRunning()) {
                Logger::warn("下载线程未在超时内结束, 强制终止");
                t->terminate();
                t->wait(1000);
            }
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
                    if (e != m_entries.end()) {
                        e->task.currentClipTitle = title;
                        if (clipIndex >= 0 && clipIndex < e->task.clipProgress.size())
                            e->task.clipProgress[clipIndex].status = model::TaskStatus::Downloading;
                    }
                }
                emit taskClipStarted(id, clipIndex, title);
            }, Qt::QueuedConnection);

    // 分P进度: 存入快照并向外转发 (UI 展开视图)
    connect(runner, &TaskRunner::clipProgress, this,
            [this](int id, int clipIndex, qint64 received, qint64 total, double speed) {
                {
                    QMutexLocker l(&m_mutex);
                    auto e = m_entries.find(id);
                    if (e != m_entries.end() && clipIndex >= 0 && clipIndex < e->task.clipProgress.size()) {
                        auto &cp = e->task.clipProgress[clipIndex];
                        cp.received = received; cp.total = total; cp.speed = speed;
                    }
                }
                emit clipProgress(id, clipIndex, received, total, speed);
            }, Qt::QueuedConnection);

    // 分P完成: 同步 doneClips 到引擎副本 (修复进度显示 0/x 的根因) + 转发
    connect(runner, &TaskRunner::clipDone, this,
            [this](int id, int clipIndex, const QString &file, int doneCount, int totalCount,
                   const QString &sha256) {
                {
                    QMutexLocker l(&m_mutex);
                    auto e = m_entries.find(id);
                    if (e != m_entries.end()) {
                        e->task.doneClips = doneCount;
                        e->task.totalClips = totalCount;
                        if (clipIndex >= 0 && clipIndex < e->task.clipProgress.size()) {
                            auto &cp = e->task.clipProgress[clipIndex];
                            cp.status = model::TaskStatus::Success;
                            cp.outputFile = file;
                            cp.received = cp.total;
                            cp.sha256 = sha256;
                        }
                        // 非合并模式: 同步输出文件列表 (供 UI 显示产物路径)
                        if (!file.isEmpty() && !e->task.outputFiles.contains(file))
                            e->task.outputFiles.append(file);
                    }
                }
                emit clipDone(id, clipIndex, file, doneCount, totalCount, sha256);
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

// ---------------- 快照 / 恢复 / 持久化 ----------------
QList<bili::model::DownloadTask> DownloadEngine::snapshotAll() const {
    QMutexLocker locker(&m_mutex);
    QList<bili::model::DownloadTask> out;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it)
        out.append(it->task);
    return out;
}

QList<int> DownloadEngine::restoreTasks(const QList<bili::model::DownloadTask> &tasks) {
    QList<int> ids;
    for (const auto &t : tasks) {
        int id = addTask(t);
        ids << id;
    }
    Logger::info(QString("已恢复 %1 个任务 (待用户确认继续)").arg(ids.size()));
    return ids;
}

void DownloadEngine::saveTasksToDisk() {
    QList<bili::model::DownloadTask> snapshots = snapshotAll();
    QJsonArray arr;
    for (const auto &t : snapshots) {
        // 已完成/失败/已停止的任务不恢复; 只持久化排队中/下载中/暂停的任务
        if (t.status == model::TaskStatus::Success)
            continue;
        arr.append(taskToJson(t));
    }
    QFile f(tasksFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Logger::warn("任务持久化失败: 无法写入 " + tasksFilePath());
        return;
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.close();
    Logger::info(QString("任务已持久化: %1 个 (未完成) -> %2").arg(arr.size()).arg(tasksFilePath()));
}

void DownloadEngine::loadTasksFromDisk() {
    QFile f(tasksFilePath());
    if (!f.exists()) return;
    if (!f.open(QIODevice::ReadOnly)) {
        Logger::warn("任务恢复失败: 无法读取 " + tasksFilePath());
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isArray()) return;
    QList<model::DownloadTask> tasks;
    for (const QJsonValue &v : doc.array()) {
        auto t = taskFromJson(v.toObject());
        if (!t.videoInfo.clips.isEmpty())
            tasks.append(t);
    }
    if (tasks.isEmpty()) return;
    restoreTasks(tasks); // 恢复为 Queued; 由主窗口提示用户是否继续
}

} // namespace bili::downloader
