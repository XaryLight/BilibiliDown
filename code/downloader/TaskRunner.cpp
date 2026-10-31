#include "TaskRunner.h"
#include "impl/Parsers.h"

#include <api/HttpClient.h>
#include <api/Ffmpeg.h>
#include <api/Logger.h>
#include <api/Config.h>
#include <api/Crypto.h>

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QRegularExpression>

namespace bili::downloader {

using bili::api::HttpClient;
using bili::api::Ffmpeg;
using bili::api::Config;
using bili::api::Logger;

TaskRunner::TaskRunner(model::DownloadTask task, QObject *parent)
    : QObject(parent), m_task(std::move(task)) {
    // BvParser 仅用于复用 AbstractBaseParser::getVideoLink 的链接查询能力
    m_linkQuerier = std::make_unique<parser::BvParser>();
}

TaskRunner::~TaskRunner() = default;

QString TaskRunner::sanitizeName(const QString &raw) {
    QString s = raw;
    // 去除文件系统非法字符 (与 Java 版一致)
    s.replace(QRegularExpression(R"RE([\t\b\r\n|:*?<>\"$])RE"), "_");
    // 路径分隔符统一为当前系统分隔符
    s.replace('/', QDir::separator());
    s.replace('\\', QDir::separator());
    // 去除 ".." 防止路径逃逸
    s.remove(QRegularExpression(R"RE(\.{2,})RE"));
    s = s.trimmed();
    if (s.size() > 120) s = s.left(120);
    return s;
}

QString TaskRunner::uniqueFilePath(const QString &dir, const QString &baseName, const QString &suffix) {
    QString candidate = QDir(dir).absoluteFilePath(baseName + suffix);
    if (!QFileInfo::exists(candidate))
        return candidate;
    for (int i = 1; i < 100; i++) {
        QString c = QDir(dir).absoluteFilePath(
            QString("%1(%2)%3").arg(baseName).arg(i, 2, 10, QChar('0')).arg(suffix));
        if (!QFileInfo::exists(c))
            return c;
    }
    return candidate;
}

void TaskRunner::run() {
    bool ok = false;
    QString error;
    try {
        ok = processClipAll(error);
    } catch (const std::exception &e) {
        error = e.what();
        Logger::error(QString("任务%1 异常: %2").arg(m_task.taskId).arg(error));
    }

    if (m_stop.load(std::memory_order_relaxed)) {
        emit statusChanged(m_task.taskId, model::TaskStatus::Stopped, "已停止");
    } else if (ok) {
        emit statusChanged(m_task.taskId, model::TaskStatus::Success, "完成");
    } else {
        emit statusChanged(m_task.taskId, model::TaskStatus::Failed,
                          error.isEmpty() ? "下载失败" : error);
    }
    emit finished(m_task.taskId, ok && !m_stop.load(std::memory_order_relaxed), error);
}

bool TaskRunner::processClipAll(QString &error) {
    QDir saveDir(Config::instance().savePath());
    if (!saveDir.exists() && !saveDir.mkpath(".")) {
        error = "无法创建下载目录: " + saveDir.absolutePath();
        return false;
    }
    const QString savePath = saveDir.absolutePath();

    m_task.totalClips = static_cast<int>(m_task.videoInfo.clips.size());
    m_task.doneClips = 0;
    if (m_task.totalClips == 0) {
        error = "没有可下载的分P";
        return false;
    }

    // 合集合并模式: 建立临时目录存放中间 mp4
    const bool mergeMode = m_task.mergeCollection && m_task.totalClips > 1;
    QString mergeTmpDir;
    QStringList intermediateFiles;
    if (mergeMode) {
        mergeTmpDir = savePath + "/" + sanitizeName(m_task.videoInfo.videoName) + "_parts";
        QDir().mkpath(mergeTmpDir);
        Logger::info(QString("合集合并模式: %1 个分P -> 临时目录 %2")
                         .arg(m_task.totalClips).arg(mergeTmpDir));
    }

    // ffmpeg 探测 (仅在当前不可用时自动探测, 避免覆盖已设置路径)
    Ffmpeg &ffmpeg = Ffmpeg::instance();
    if (!ffmpeg.available()) {
        ffmpeg.autoDetect();
        if (!ffmpeg.available()) {
            Logger::warn("ffmpeg 不可用, 合并/转码步骤可能失败 (可在设置中指定 ffmpeg 路径)");
        } else {
            Logger::info("ffmpeg 自动探测成功: " + ffmpeg.executable());
        }
    }

    for (int i = 0; i < m_task.totalClips; i++) {
        if (m_stop.load(std::memory_order_relaxed))
            return false;

        const model::ClipInfo &clip = m_task.videoInfo.clips.at(i);
        emit clipStarted(m_task.taskId, i, clip.title);

        QString outFile;
        try {
            QString workDir = mergeMode ? mergeTmpDir : savePath;
            outFile = processClip(clip, i, workDir,
                                  mergeMode ? QString("clip_%1").arg(i + 1, 3, 10, QChar('0'))
                                            : QString(),
                                  mergeMode);
        } catch (const std::exception &e) {
            error = QString("分P %1 (%2) 处理失败: %3").arg(i + 1).arg(clip.title, e.what());
            Logger::error(error);
            return false;
        }

        if (m_stop.load(std::memory_order_relaxed))
            return false;

        m_task.doneClips = i + 1;
        if (mergeMode) {
            intermediateFiles << outFile;
            emit statusChanged(m_task.taskId, model::TaskStatus::Downloading,
                              QString("已下载 %1/%2").arg(m_task.doneClips).arg(m_task.totalClips));
        } else {
            m_task.outputFiles << outFile;
            emit statusChanged(m_task.taskId, model::TaskStatus::Downloading,
                              QString("已完成 %1/%2").arg(m_task.doneClips).arg(m_task.totalClips));
        }
        emit clipFinished(m_task.taskId, i, outFile);
    }

    // ---- 合集合并为单个 MP4 ----
    if (mergeMode && !m_stop.load(std::memory_order_relaxed)) {
        emit statusChanged(m_task.taskId, model::TaskStatus::Merging, "正在合并合集...");
        QString finalPath = uniqueFilePath(savePath, sanitizeName(m_task.videoInfo.videoName), ".mp4");
        if (!mergeClips(intermediateFiles, finalPath)) {
            error = "合集合并失败: " + m_task.videoInfo.videoName;
            return false;
        }
        m_task.outputFile = finalPath;
        m_task.outputFiles << finalPath;
        // 清理中间文件
        if (m_task.deleteTempAfterMerge) {
            for (const QString &f : intermediateFiles)
                QFile::remove(f);
            QDir().rmdir(mergeTmpDir);
            Logger::info("已清理中间文件: " + mergeTmpDir);
        }
    }
    return true;
}

QString TaskRunner::processClip(const model::ClipInfo &clip, int clipIndex,
                                const QString &workDir, const QString &finalBaseName,
                                bool forMerge) {
    Q_UNUSED(clipIndex)
    const QString avId = clip.avId;
    QString cid = QString::number(clip.cid);
    const int qn = m_task.qn;
    const int format = m_task.format;

    // UP主空间列表等场景: 解析期 vlist 不含 cid, 此处按 bvid+分P号懒加载补查
    if (clip.cid <= 0) {
        emit statusChanged(m_task.taskId, model::TaskStatus::Downloading,
                          QString("解析 p%1 cid...").arg(clip.page));
        int64_t realCid = m_linkQuerier->resolveCid(avId, clip.page);
        if (realCid <= 0)
            throw std::runtime_error("cid 解析失败: " + avId.toStdString());
        cid = QString::number(realCid);
        Logger::info(QString("任务%1 p%2 cid 懒加载: %3").arg(m_task.taskId).arg(clip.page).arg(cid));
    }

    emit statusChanged(m_task.taskId, model::TaskStatus::Downloading,
                      QString("查询链接 p%1 %2").arg(clip.page).arg(clip.title));

    // ---- 1. 查询播放链接 ----
    QString link = m_linkQuerier->getVideoLink(avId, cid, qn, format);
    int realQN = m_linkQuerier->realQN();
    if (link.isEmpty())
        throw std::runtime_error("播放链接查询失败");

    Logger::info(QString("任务%1 p%2 链接(qn=%3 实际%4): %5...")
                     .arg(m_task.taskId).arg(clip.page).arg(qn).arg(realQN)
                     .arg(link.left(80)));

    // ---- 2. 确定输出文件名 ----
    QString baseName;
    if (forMerge) {
        baseName = finalBaseName; // clip_001
    } else {
        QString avTitle = sanitizeName(clip.avTitle.isEmpty() ? m_task.videoInfo.videoName : clip.avTitle);
        QString clipTitle = sanitizeName(clip.title);
        baseName = QString("%1-p%2-%3-%4").arg(avTitle).arg(clip.page).arg(clipTitle).arg(realQN);
        baseName.remove(QRegularExpression(R"RE([\\/]+)RE")); // 文件名内不允许有路径
        baseName = baseName.trimmed();
        if (baseName.isEmpty()) baseName = QString("%1-p%2").arg(avId).arg(clip.page);
    }

    // ---- 3. 若最终文件已存在, 跳过 ----
    if (!forMerge) {
        QString suffix0 = (format == model::Format_FLV) ? ".flv"
                          : (format == model::Format_AudioOnly) ? ".m4a" : ".mp4";
        QString finalCandidate = QDir(workDir).absoluteFilePath(baseName + suffix0);
        if (QFileInfo::exists(finalCandidate)) {
            Logger::info("文件已存在, 跳过: " + finalCandidate);
            return finalCandidate;
        }
    }

    // ---- 4. 下载 ----
    // 速度统计上下文
    auto progressCb = [this](const HttpClient::Progress &p) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_lastSpeedTime == 0) {
            m_lastSpeedTime = now;
            m_lastReceived = 0;
        }
        qint64 dt = now - m_lastSpeedTime;
        if (dt >= 500) {
            m_speed = (p.received - m_lastReceived) * 1000.0 / static_cast<double>(dt);
            m_lastSpeedTime = now;
            m_lastReceived = p.received;
        }
        emit progress(m_task.taskId, p.received, p.total, m_speed);
    };
    auto isCancelled = [this]() { return m_stop.load(std::memory_order_relaxed); };
    api::Headers downHeaders = HttpClient::m4sHeaders();

    Ffmpeg &ffmpeg = Ffmpeg::instance();
    bool convertOk = false;

    if (format == model::Format_FLV) {
        // FLV: "order1url1#order2url2..." 或单个 url
        QStringList segs = link.split('#');
        QString dstName = QDir(workDir).absoluteFilePath(baseName + ".flv");
        if (segs.size() == 1) {
            QString dest = dstName;
            emit statusChanged(m_task.taskId, model::TaskStatus::Downloading,
                              QString("下载 p%1 %2").arg(clip.page).arg(clip.title));
            QString err;
            if (!HttpClient::instance().downloadFile(segs.first(), dest, downHeaders,
                                                     progressCb, isCancelled, &err))
                throw std::runtime_error("FLV 下载失败: " + err.toStdString());
            convertOk = true;
        } else {
            // 多段 flv: 逐段下载后 concat 合并
            QStringList partFiles;
            for (int i = 0; i < segs.size(); i++) {
                if (m_stop.load(std::memory_order_relaxed))
                    throw std::runtime_error("已停止");
                QString partUrl = segs.at(i);
                static const QRegularExpression orderRe(R"RE(^(\d+)(https?://.*)$)RE");
                auto m = orderRe.match(partUrl);
                if (m.hasMatch())
                    partUrl = m.captured(2);
                QString partFinal = QDir(workDir).absoluteFilePath(
                    QString("%1-part%2.flv").arg(baseName).arg(i + 1));
                if (QFileInfo::exists(partFinal)) {
                    partFiles << partFinal;
                    continue;
                }
                emit statusChanged(m_task.taskId, model::TaskStatus::Downloading,
                                  QString("下载 p%1 分片%2/%3")
                                      .arg(clip.page).arg(i + 1).arg(segs.size()));
                QString err;
                if (!HttpClient::instance().downloadFile(partUrl, partFinal, downHeaders,
                                                         progressCb, isCancelled, &err))
                    throw std::runtime_error(QString("FLV 分片 %1 下载失败: %2")
                                                 .arg(i + 1).arg(err).toStdString());
                partFiles << partFinal;
            }
            emit statusChanged(m_task.taskId, model::TaskStatus::Converting, "合并 FLV 分片...");
            QString err;
            convertOk = ffmpeg.concat(partFiles, dstName, true, nullptr, &err);
            if (!convertOk)
                Logger::error("FLV concat 失败: " + err);
            if (convertOk) {
                for (const QString &f : partFiles) QFile::remove(f);
            }
            if (convertOk)
                return dstName;
            throw std::runtime_error("FLV 合并失败");
        }
        if (convertOk)
            return dstName;
        throw std::runtime_error("FLV 下载失败");
    }

    // DASH (MP4) / 仅音频
    QStringList parts = link.split('#');
    const bool audioOnly = link.startsWith('#');
    QString videoName = QDir(workDir).absoluteFilePath(baseName + "_video.m4s");
    QString audioName;
    if (!audioOnly && parts.size() > 1 && !parts.at(1).isEmpty())
        audioName = QDir(workDir).absoluteFilePath(baseName + "_audio.m4s");
    if (audioOnly)
        audioName = QDir(workDir).absoluteFilePath(baseName + "_audio.m4s");
    QString suffix = (m_task.format == model::Format_AudioOnly) ? ".m4a" : ".mp4";
    QString dstName = QDir(workDir).absoluteFilePath(baseName + suffix);

    // 下载视频流
    if (!audioOnly) {
        emit statusChanged(m_task.taskId, model::TaskStatus::Downloading,
                          QString("下载视频 p%1 %2").arg(clip.page).arg(clip.title));
        QString err;
        if (!HttpClient::instance().downloadFile(parts.first(), videoName, downHeaders,
                                                 progressCb, isCancelled, &err))
            throw std::runtime_error("视频流下载失败: " + err.toStdString());
    }
    // 下载音频流
    if (!audioName.isEmpty()) {
        QString audioUrl = audioOnly ? parts.at(1) : parts.at(1);
        emit statusChanged(m_task.taskId, model::TaskStatus::Downloading,
                          QString("下载音频 p%1 %2").arg(clip.page).arg(clip.title));
        QString err;
        if (!HttpClient::instance().downloadFile(audioUrl, audioName, downHeaders,
                                                 progressCb, isCancelled, &err))
            throw std::runtime_error("音频流下载失败: " + err.toStdString());
    }

    if (m_stop.load(std::memory_order_relaxed))
        throw std::runtime_error("已停止");

    // 合并/转码
    emit statusChanged(m_task.taskId, model::TaskStatus::Converting,
                      audioOnly ? "音频转码..." : "音视频合并...");
    QString err;
    if (audioOnly) {
        convertOk = ffmpeg.mergeVideoAudio(QString(), audioName, dstName, &err);
        if (!convertOk && QFile::rename(audioName, dstName))
            convertOk = true; // 转码失败时退而求其次, 保留 m4s 内容
    } else {
        convertOk = ffmpeg.mergeVideoAudio(videoName, audioName, dstName, &err);
        if (!convertOk && audioName.isEmpty())
            convertOk = ffmpeg.remux(videoName, dstName, &err);
    }
    // 清理 m4s 中间文件
    if (convertOk) {
        QFile::remove(videoName);
        if (!audioName.isEmpty())
            QFile::remove(audioName);
    }

    if (!convertOk || !QFileInfo::exists(dstName) || QFileInfo(dstName).size() == 0)
        throw std::runtime_error("转码/合并失败: " + err.toStdString());

    return dstName;
}

bool TaskRunner::mergeClips(const QStringList &clipFiles, const QString &finalPath) {
    Ffmpeg &ffmpeg = Ffmpeg::instance();
    auto progress = [this](int done, int total) {
        emit statusChanged(m_task.taskId, model::TaskStatus::Merging,
                          QString("正在合并合集 %1/%2").arg(done).arg(total));
    };
    QString err;
    bool ok = ffmpeg.mergeClipsToSingleMp4(clipFiles, finalPath, progress, &err);
    if (!ok)
        Logger::error("合集合并失败: " + err);
    return ok && QFileInfo::exists(finalPath) && QFileInfo(finalPath).size() > 0;
}

} // namespace bili::downloader
