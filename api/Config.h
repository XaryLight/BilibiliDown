#ifndef BILI_API_CONFIG_H
#define BILI_API_CONFIG_H

#include <QString>
#include <QVariant>
#include <QSettings>

namespace bili::api {

/**
 * @brief 应用配置 (QSettings 持久化)
 *        键名与 Java 版 app.config 对应, 但存储为 ini/registry.
 */
class Config {
public:
    static Config &instance();

    void load();
    void save();
    void resetToDefault();

    QVariant get(const QString &key, const QVariant &def = QVariant()) const;
    void set(const QString &key, const QVariant &value);

    // ---- 常用配置项 ----
    QString savePath() const;            void setSavePath(const QString &);
    int     maxDownloadThreads() const;  void setMaxDownloadThreads(int);
    int     downloadFormat() const;      void setDownloadFormat(int);   // 0 MP4(DASH) 1 FLV 2 仅音频
    int     defaultQuality() const;      void setDefaultQuality(int);   // 127/120/116/112/80/64/32/16
    bool    mergeCollection() const;     void setMergeCollection(bool); // 合集合并为单个MP4
    bool    deleteTempAfterMerge() const; void setDeleteTempAfterMerge(bool);
    QString ffmpegPath() const;          void setFfmpegPath(const QString &);
    QString cookies() const;             void setCookies(const QString &);
    QString userAgent() const;           void setUserAgent(const QString &);
    bool    checkDashUrl() const;        void setCheckDashUrl(bool);
    int     videoCodecPriority() const;  void setVideoCodecPriority(int);
    int     audioQualityPriority() const; void setAudioQualityPriority(int);
    bool    autoRename() const;          void setAutoRename(bool);
    bool    minimizeToTray() const;      void setMinimizeToTray(bool); // 关闭窗口时最小化到任务栏托盘
    bool    notifyOnFinish() const;      void setNotifyOnFinish(bool); // 任务完成时弹出气泡通知

private:
    Config() = default;
    QSettings m_settings{"BilibiliDown", "BilibiliDown"};
    bool m_loaded = false;
};

} // namespace bili::api

#endif // BILI_API_CONFIG_H
