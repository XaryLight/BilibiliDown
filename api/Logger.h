#ifndef BILI_API_LOGGER_H
#define BILI_API_LOGGER_H

#include <QObject>
#include <QString>
#include <QMutex>
#include <QFile>

namespace bili::api {

/**
 * @brief 简单日志器: 输出到控制台 + 内存环形缓冲 + 日志文件
 *        单例, 线程安全. 信号 logAppended 供 GUI 实时显示.
 */
class Logger : public QObject {
    Q_OBJECT
public:
    static Logger &instance();

    void init(const QString &logDir = QString());
    void setLogFile(const QString &path);

    // 静态便捷接口
    static void debug(const QString &msg);
    static void info(const QString &msg);
    static void warn(const QString &msg);
    static void error(const QString &msg);

    QStringList recentLines(int max = 500) const;

signals:
    void logAppended(const QString &line, int level);

private:
    explicit Logger(QObject *parent = nullptr);
    void write(int level, const QString &msg);
    QString logFilePath() const;

    mutable QMutex m_mutex;
    QString m_logDir;
    QFile m_file;
    QStringList m_recent;
    bool m_inited = false;
};

} // namespace bili::api

#endif // BILI_API_LOGGER_H
