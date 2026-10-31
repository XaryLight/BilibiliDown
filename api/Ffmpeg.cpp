#include "Ffmpeg.h"
#include "Logger.h"
#include "Config.h"

#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <QTemporaryFile>
#include <QTextStream>
#include <QCoreApplication>
#include <functional>
#include <algorithm>

namespace bili::api {

Ffmpeg &Ffmpeg::instance() {
    static Ffmpeg s;
    return s;
}

void Ffmpeg::setExecutable(const QString &path) {
    m_executable = path;
}

QString Ffmpeg::executable() const {
    return m_executable;
}

bool Ffmpeg::available(QString *versionOut) const {
    QProcess p;
    p.start(m_executable, {"-version"});
    if (!p.waitForStarted(5000)) {
        if (versionOut) *versionOut = "无法启动 ffmpeg";
        return false;
    }
    if (!p.waitForFinished(10000)) {
        p.kill();
        if (versionOut) *versionOut = "ffmpeg 响应超时";
        return false;
    }
    QString out = QString::fromUtf8(p.readAllStandardOutput());
    if (versionOut) *versionOut = out.section('\n', 0, 0);
    return p.exitCode() == 0 && !out.isEmpty();
}

bool Ffmpeg::autoDetect() {
    // 若当前可执行文件已是有效的绝对路径, 保持不变 (不覆盖外部显式设置)
    QFileInfo cur(m_executable);
    if (cur.isAbsolute() && cur.exists()) {
        Logger::info("沿用当前 ffmpeg: " + m_executable);
        return true;
    }
    // 优先使用配置中设置的路径
    QString cfgPath = Config::instance().ffmpegPath();
    if (!cfgPath.isEmpty() && cfgPath != "ffmpeg") {
        QFileInfo fi(cfgPath);
        if (fi.exists()) {
            m_executable = cfgPath;
            Logger::info("使用配置的 ffmpeg: " + cfgPath);
            return true;
        }
    }
    // 依次探测常见位置
    QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/bin/ffmpeg.exe",
        appDir + "/ffmpeg.exe",
#ifdef Q_OS_WIN
        "C:/ffmpeg/bin/ffmpeg.exe",
#endif
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c)) {
            m_executable = c;
            Logger::info("发现 ffmpeg: " + c);
            return true;
        }
    }
    // 回退 PATH
    m_executable = "ffmpeg";
    return false;
}

int Ffmpeg::run(const QStringList &args, const QString &workingDir, QString *stderrOut) const {
    Logger::info("ffmpeg: " + args.join(' '));
    QProcess p;
    if (!workingDir.isEmpty())
        p.setWorkingDirectory(workingDir);
    p.start(m_executable, args);
    if (!p.waitForStarted(10000)) {
        QString msg = "ffmpeg 启动失败: " + p.errorString();
        Logger::error(msg);
        if (stderrOut && stderrOut->isEmpty()) *stderrOut = msg;
        return -1;
    }
    // 等待结束 (ffmpeg 合并长视频可能需要较久, 不设上限由调用方控制)
    // 注意: 必须持续读取 stderr, 避免管道缓冲区写满导致 ffmpeg 阻塞死锁
    QString errBuf;
    QEventLoop loop;
    QObject::connect(&p, &QProcess::readyReadStandardError, &p, [&]() {
        errBuf += QString::fromUtf8(p.readAllStandardError());
    });
    QObject::connect(&p, &QProcess::finished, &loop, &QEventLoop::quit);
    loop.exec();
    errBuf += QString::fromUtf8(p.readAllStandardError());
    if (stderrOut) *stderrOut = errBuf;
    int code = p.exitCode();
    if (code != 0) {
        Logger::warn(QString("ffmpeg 退出码 %1, 尾部输出: %2")
                         .arg(code).arg(errBuf.right(500)));
        if (stderrOut && stderrOut->isEmpty())
            *stderrOut = QString("ffmpeg 退出码 %1").arg(code);
    }
    return code;
}

bool Ffmpeg::mergeVideoAudio(const QString &videoPath, const QString &audioPath,
                             const QString &dstPath, QString *errorOut) const {
    if (QFileInfo::exists(dstPath) && QFileInfo(dstPath).size() > 0) {
        Logger::info("目标已存在, 跳过合并: " + dstPath);
        return true;
    }
    QStringList args{"-y"};
    if (!videoPath.isEmpty())
        args << "-i" << videoPath;
    if (!audioPath.isEmpty())
        args << "-i" << audioPath;
    if (videoPath.isEmpty() && audioPath.isEmpty()) {
        if (errorOut) *errorOut = "合并参数为空";
        return false;
    }
    args << "-c" << "copy";
    if (videoPath.isEmpty()) {
        // 仅音频
        args << "-vn";
    }
    if (audioPath.isEmpty()) {
        args << "-an";
    }
    args << dstPath;

    int code = run(args, QFileInfo(dstPath).absolutePath(), errorOut);
    if (code != 0) {
        // 流拷贝失败时回退: 重编码合并
        Logger::warn("流拷贝合并失败, 尝试重编码");
        QStringList args2{"-y"};
        if (!videoPath.isEmpty()) args2 << "-i" << videoPath;
        if (!audioPath.isEmpty()) args2 << "-i" << audioPath;
        args2 << "-c:v" << "libx264" << "-preset" << "veryfast" << "-c:a" << "aac";
        if (videoPath.isEmpty()) args2 << "-vn";
        if (audioPath.isEmpty()) args2 << "-an";
        args2 << dstPath;
        code = run(args2, QFileInfo(dstPath).absolutePath(), errorOut);
    }
    bool ok = (code == 0) && QFileInfo::exists(dstPath) && QFileInfo(dstPath).size() > 0;
    if (!ok && errorOut && errorOut->isEmpty())
        *errorOut = "ffmpeg 合并失败";
    return ok;
}

bool Ffmpeg::remux(const QString &srcPath, const QString &dstPath, QString *errorOut) const {
    if (QFileInfo::exists(dstPath) && QFileInfo(dstPath).size() > 0) {
        Logger::info("目标已存在, 跳过转封装: " + dstPath);
        return true;
    }
    QStringList args{"-y", "-i", srcPath, "-c", "copy", dstPath};
    int code = run(args, QFileInfo(dstPath).absolutePath(), errorOut);
    bool ok = (code == 0) && QFileInfo::exists(dstPath) && QFileInfo(dstPath).size() > 0;
    if (!ok && errorOut && errorOut->isEmpty())
        *errorOut = "ffmpeg 转封装失败";
    return ok;
}

bool Ffmpeg::concat(const QStringList &srcFiles, const QString &dstPath,
                    bool allowReencode,
                    const std::function<void(int, int)> &progress,
                    QString *errorOut) const {
    if (srcFiles.isEmpty()) {
        if (errorOut) *errorOut = "拼接文件列表为空";
        return false;
    }
    if (srcFiles.size() == 1) {
        // 只有一个文件, 直接拷贝
        if (QFile::remove(dstPath) || !QFile::exists(dstPath)) {
            if (QFile::copy(srcFiles.first(), dstPath)) {
                if (progress) progress(1, 1);
                return true;
            }
        }
    }

    QString workDir = QFileInfo(dstPath).absolutePath();
    // 生成 concat 列表文件 (ffmpeg concat demuxer)
    // 注意: concat demuxer 的引号内反斜杠是转义字符, 必须使用正斜杠路径;
    //       列表中的相对路径是相对于【列表文件所在目录】解析的;
    //       若所有源文件位于同一目录, 则仅写文件名, 并将列表文件也放入该目录,
    //       这样可完全规避路径中的特殊字符 (空格/引号/中文) 问题
    QString commonDir = QFileInfo(srcFiles.first()).absolutePath();
    bool sameDir = std::all_of(srcFiles.begin(), srcFiles.end(), [&](const QString &f) {
        return QFileInfo(f).absolutePath() == commonDir;
    });
    if (sameDir)
        workDir = commonDir;
    // 列表文件放置目录: 同目录时放源文件目录, 否则放目标目录
    QString listDir = sameDir ? commonDir : QFileInfo(dstPath).absolutePath();
    QString listPath = listDir + "/" + QFileInfo(dstPath).fileName() + ".concat.txt";
    {
        QFile f(listPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (errorOut) *errorOut = "无法写入 concat 列表";
            return false;
        }
        QTextStream ts(&f);
        for (const QString &src : srcFiles) {
            QString entry = sameDir ? QFileInfo(src).fileName()
                                    : QDir::toNativeSeparators(src).replace('\\', '/');
            ts << "file '" << entry << "'\n";
        }
    }

    // 阶段1: 流拷贝拼接
    Logger::info(QString("开始合并 %1 个文件为 %2").arg(srcFiles.size()).arg(dstPath));
    if (progress) progress(0, static_cast<int>(srcFiles.size()));

    QStringList args{"-y", "-f", "concat", "-safe", "0", "-i", listPath,
                     "-c", "copy", "-movflags", "+faststart", dstPath};
    QString err;
    int code = run(args, workDir, &err);
    bool ok = (code == 0) && QFileInfo::exists(dstPath) && QFileInfo(dstPath).size() > 0;
    if (ok) {
        if (progress) progress(static_cast<int>(srcFiles.size()), static_cast<int>(srcFiles.size()));
    }

    if (!ok && allowReencode) {
        // 阶段2: 参数不一致时回退重编码拼接
        Logger::warn("流拷贝拼接失败, 回退为统一重编码后拼接");
        // 先将所有输入统一转码为相同参数的中间文件
        QStringList normFiles;
        bool normOk = true;
        for (int i = 0; i < srcFiles.size(); ++i) {
            QString normPath = QDir::tempPath() + QString("/bilidown_norm_%1.mp4").arg(i);
            QStringList a{"-y", "-i", srcFiles[i], "-c:v", "libx264", "-preset", "veryfast",
                          "-pix_fmt", "yuv420p", "-r", "30", "-c:a", "aac", "-ar", "44100",
                          normPath};
            int c = run(a, workDir);
            if (c != 0 || !QFileInfo::exists(normPath) || QFileInfo(normPath).size() == 0) {
                normOk = false;
                break;
            }
            normFiles << normPath;
            if (progress) progress(i + 1, static_cast<int>(srcFiles.size()) * 2);
        }
        if (normOk) {
            // 写新的 concat 列表 (同样使用正斜杠)
            {
                QFile f(listPath);
                if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    QTextStream ts(&f);
                    for (const QString &src : normFiles) {
                        QString p = QDir::toNativeSeparators(src).replace('\\', '/');
                        ts << "file '" << p << "'\n";
                    }
                }
            }
            QStringList args2{"-y", "-f", "concat", "-safe", "0", "-i", listPath,
                              "-c", "copy", "-movflags", "+faststart", dstPath};
            code = run(args2, workDir, &err);
            ok = (code == 0) && QFileInfo::exists(dstPath) && QFileInfo(dstPath).size() > 0;
            if (ok && progress)
                progress(static_cast<int>(srcFiles.size()) * 2, static_cast<int>(srcFiles.size()) * 2);
        }
        // 清理归一化临时文件
        for (const QString &p : normFiles)
            QFile::remove(p);
    }

    QFile::remove(listPath);
    if (!ok && errorOut && errorOut->isEmpty())
        *errorOut = "ffmpeg concat 失败: " + err.right(300);
    Logger::info(ok ? QString("合并完成: %1").arg(dstPath)
                    : QString("合并失败: %1").arg(dstPath));
    return ok;
}

bool Ffmpeg::mergeClipsToSingleMp4(const QStringList &clipMp4s, const QString &dstPath,
                                   const std::function<void(int, int)> &progress,
                                   QString *errorOut) const {
    return concat(clipMp4s, dstPath, true, progress, errorOut);
}

} // namespace bili::api
