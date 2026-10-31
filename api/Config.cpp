#include "Config.h"
#include "Logger.h"

#include <QCoreApplication>
#include <QStandardPaths>

namespace bili::api {

Config &Config::instance() {
    static Config s_instance;
    return s_instance;
}

void Config::load() {
    if (m_loaded) return;
    m_loaded = true;
    // QSettings 惰性加载, 这里做一次默认值补齐
    if (!m_settings.contains("savePath")) {
        QString def = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (def.isEmpty()) def = QCoreApplication::applicationDirPath() + "/download";
        m_settings.setValue("savePath", def + "/BilibiliDown");
    }
    m_settings.sync();
    Logger::info(QString("配置加载: savePath=%1").arg(savePath()));
}

void Config::save() {
    m_settings.sync();
}

void Config::resetToDefault() {
    m_settings.clear();
    m_loaded = false;
    load();
}

QVariant Config::get(const QString &key, const QVariant &def) const {
    return m_settings.value(key, def);
}

void Config::set(const QString &key, const QVariant &value) {
    m_settings.setValue(key, value);
}

QString Config::savePath() const { return get("savePath").toString(); }
void Config::setSavePath(const QString &v) { set("savePath", v); }

int Config::maxDownloadThreads() const { return get("maxDownloadThreads", 4).toInt(); }
void Config::setMaxDownloadThreads(int v) { set("maxDownloadThreads", v); }

int Config::downloadFormat() const { return get("downloadFormat", 0).toInt(); }
void Config::setDownloadFormat(int v) { set("downloadFormat", v); }

int Config::defaultQuality() const { return get("defaultQuality", 80).toInt(); }
void Config::setDefaultQuality(int v) { set("defaultQuality", v); }

bool Config::mergeCollection() const { return get("mergeCollection", false).toBool(); }
void Config::setMergeCollection(bool v) { set("mergeCollection", v); }

bool Config::deleteTempAfterMerge() const { return get("deleteTempAfterMerge", true).toBool(); }
void Config::setDeleteTempAfterMerge(bool v) { set("deleteTempAfterMerge", v); }

QString Config::ffmpegPath() const { return get("ffmpegPath", "ffmpeg").toString(); }
void Config::setFfmpegPath(const QString &v) { set("ffmpegPath", v); }

QString Config::cookies() const { return get("cookies").toString(); }
void Config::setCookies(const QString &v) { set("cookies", v); }

QString Config::userAgent() const {
    return get("userAgent",
               "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
               "(KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36").toString();
}
void Config::setUserAgent(const QString &v) { set("userAgent", v); }

bool Config::checkDashUrl() const { return get("checkDashUrl", false).toBool(); }
void Config::setCheckDashUrl(bool v) { set("checkDashUrl", v); }

int Config::videoCodecPriority() const { return get("videoCodecPriority", 7).toInt(); }
void Config::setVideoCodecPriority(int v) { set("videoCodecPriority", v); }

int Config::audioQualityPriority() const { return get("audioQualityPriority", 30280).toInt(); }
void Config::setAudioQualityPriority(int v) { set("audioQualityPriority", v); }

bool Config::autoRename() const { return get("autoRename", true).toBool(); }
void Config::setAutoRename(bool v) { set("autoRename", v); }

bool Config::minimizeToTray() const { return get("ui/minimizeToTray", true).toBool(); }
void Config::setMinimizeToTray(bool v) { set("ui/minimizeToTray", v); }

bool Config::notifyOnFinish() const { return get("ui/notifyOnFinish", true).toBool(); }
void Config::setNotifyOnFinish(bool v) { set("ui/notifyOnFinish", v); }

} // namespace bili::api
