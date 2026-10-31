#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <cstdio>

namespace bili::api {

Logger &Logger::instance() {
    static Logger s_instance;
    return s_instance;
}

Logger::Logger(QObject *parent) : QObject(parent) {}

void Logger::init(const QString &logDir) {
    QMutexLocker locker(&m_mutex);
    if (m_inited) return;
    m_inited = true;

    m_logDir = logDir;
    if (m_logDir.isEmpty()) {
        m_logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (m_logDir.isEmpty()) m_logDir = QDir::currentPath();
    }
    QDir().mkpath(m_logDir);

    QString path = m_logDir + "/BilibiliDown.log";
    m_file.setFileName(path);
    if (m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        // 启动即写入一条
        QString line = QString("[%1] ==== 日志初始化 ====")
                           .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
        m_file.write(line.toUtf8() + "\n");
        m_file.flush();
    }
}

void Logger::setLogFile(const QString &path) {
    QMutexLocker locker(&m_mutex);
    if (m_file.isOpen()) m_file.close();
    m_file.setFileName(path);
    if (m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        m_file.flush();
    }
}

void Logger::write(int level, const QString &msg) {
    static const char *kLevelNames[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    QString line = QString("[%1] [%2] %3")
                       .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"),
                            QString(kLevelNames[level < 0 || level > 3 ? 1 : level]),
                            msg);

    {
        QMutexLocker locker(&m_mutex);
        m_recent << line;
        if (m_recent.size() > 2000) m_recent.removeFirst();
        if (m_file.isOpen()) {
            m_file.write(line.toUtf8() + "\n");
            m_file.flush();
        }
    }

    // 控制台输出
    fprintf(level >= 2 ? stderr : stdout, "%s\n", line.toLocal8Bit().constData());
    fflush(level >= 2 ? stderr : stdout);

    emit logAppended(line, level);
}

void Logger::debug(const QString &msg) { instance().write(0, msg); }
void Logger::info(const QString &msg)  { instance().write(1, msg); }
void Logger::warn(const QString &msg)  { instance().write(2, msg); }
void Logger::error(const QString &msg) { instance().write(3, msg); }

QStringList Logger::recentLines(int max) const {
    QMutexLocker locker(&m_mutex);
    if (m_recent.size() <= max) return m_recent;
    return m_recent.mid(m_recent.size() - max);
}

} // namespace bili::api
