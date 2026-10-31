#include "Fingerprint.h"
#include "Config.h"
#include "HttpClient.h"
#include "Logger.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QRecursiveMutex>
#include <QRandomGenerator>
#include <QUrlQuery>

namespace bili::api {

// 配置键
static const char *kKeyCookie = "network/fingerprintCookie";

Fingerprint &Fingerprint::instance() {
    static Fingerprint s_instance;
    return s_instance;
}

// ---------------- 工具 ----------------
static QString randHex(int bytes) {
    QByteArray ba;
    ba.resize(bytes);
    for (int i = 0; i < bytes; ++i)
        ba[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    return QString::fromUtf8(ba.toHex());
}

// 生成 b_lsid: 8位随机hex + "_" + 当前秒的HEX (Java 版同规则)
static QString genBLsid() {
    return randHex(8) + "_" + QString::number(QDateTime::currentSecsSinceEpoch(), 16).toUpper();
}

// 从 cookie 串中取 b_lsid 的时间部分 (hex), 失败返回 -1
static qint64 bLsidTime(const QString &cookie) {
    for (const QString &kv : cookie.split(';')) {
        QString s = kv.trimmed();
        if (s.startsWith("b_lsid=")) {
            QString v = s.mid(7);
            QString t = v.section('_', 1, 1);
            bool ok = false;
            qint64 secs = t.toLongLong(&ok, 16);
            return ok ? secs : -1;
        }
    }
    return -1;
}

// 从 cookie 串中取 b_nut (unix 秒), 失败返回 -1
static qint64 bNutTime(const QString &cookie) {
    for (const QString &kv : cookie.split(';')) {
        QString s = kv.trimmed();
        if (s.startsWith("b_nut=")) {
            bool ok = false;
            qint64 v = s.mid(6).toLongLong(&ok);
            return ok ? v : -1;
        }
    }
    return -1;
}

// 从 Set-Cookie 头中收集 "name=value"
static void collectSetCookies(const QList<QNetworkReply::RawHeaderPair> &headers,
                              QHash<QString, QString> &out) {
    for (const auto &hp : headers) {
        if (QString::fromUtf8(hp.first).compare("Set-Cookie", Qt::CaseInsensitive) != 0)
            continue;
        QString value = QString::fromUtf8(hp.second);
        QString first = value.section(';', 0, 0); // 去掉 path/domain 等属性
        int eq = first.indexOf('=');
        if (eq <= 0) continue;
        QString name = first.left(eq).trimmed();
        QString val = first.mid(eq + 1).trimmed();
        if (!name.isEmpty() && !val.isEmpty())
            out.insert(name, val);
    }
}

static QString joinCookie(const QHash<QString, QString> &kv) {
    QStringList parts;
    for (auto it = kv.cbegin(); it != kv.cend(); ++it)
        parts << it.key() + "=" + it.value();
    return parts.join("; ");
}

// ---------------- 采集 ----------------
QString Fingerprint::acquire() {
    QHash<QString, QString> kv;

    // 1. 访问首页, 收集服务端下发的指纹 Cookie (buvid3/b_nut/b_ut 等)
    Logger::debug("指纹采集: 请求 www.bilibili.com ...");
    auto r1 = HttpClient::instance().getSync("https://www.bilibili.com/",
                                             HttpClient::defaultHeaders());
    if (r1.ok) {
        collectSetCookies(r1.rawHeaders, kv);
        Logger::debug(QString("指纹采集: 首页返回 %1B, 收集到 %2 个 Cookie")
                          .arg(r1.body.size()).arg(kv.size()));
    } else {
        Logger::warn(QString("指纹采集: 首页请求失败 (%1)").arg(r1.error));
    }
    if (!kv.contains("buvid3")) {
        Logger::warn("指纹采集: 首页未返回 buvid3, 风控风险较高");
    }

    // 2. 补齐本地生成的字段 (浏览器行为)
    kv.insert("i-wanna-go-back", "-1");
    kv.insert("b_lsid", genBLsid());
    kv.insert("_uuid", QString("%1-%2-%3-%4-%5infoc")
                              .arg(randHex(8), randHex(4), randHex(4), randHex(4), randHex(18)));
    kv.insert("browser_resolution", "1920-1080");

    // 3. 用当前 Cookie 换 buvid4
    auto r2 = HttpClient::instance().getSync(
        "https://api.bilibili.com/x/frontend/finger/spi",
        [&kv] {
            auto h = HttpClient::defaultHeaders();
            h["Cookie"] = joinCookie(kv);
            return h;
        }());
    if (r2.ok) {
        QJsonObject data = QJsonDocument::fromJson(r2.body).object().value("data").toObject();
        QString b4 = data.value("b_4").toString();
        if (!b4.isEmpty())
            kv.insert("buvid4", b4);
    } else {
        Logger::warn(QString("指纹采集: finger/spi 失败 (%1), 无 buvid4").arg(r2.error));
    }

    return joinCookie(kv);
}

// ---------------- 配置持久化 ----------------
bool Fingerprint::loadFromConfig() {
    QString cookie = Config::instance().get(kKeyCookie).toString();
    if (cookie.isEmpty())
        return false;
    // b_nut 超一年 → 整体重新采集
    qint64 nut = bNutTime(cookie);
    if (nut > 0 && QDateTime::currentSecsSinceEpoch() - nut > 365LL * 24 * 3600)
        return false;
    // b_lsid 超过 90 分钟 → 就地刷新
    qint64 lsid = bLsidTime(cookie);
    if (lsid > 0 && QDateTime::currentSecsSinceEpoch() - lsid > 90 * 60) {
        QString newLsid = genBLsid();
        QStringList parts;
        for (const QString &kv : cookie.split(';')) {
            QString s = kv.trimmed();
            parts << (s.startsWith("b_lsid=") ? "b_lsid=" + newLsid : s);
        }
        cookie = parts.join("; ");
        saveToConfig(cookie);
    }
    m_cookie = cookie;
    return true;
}

void Fingerprint::saveToConfig(const QString &cookie) {
    Config::instance().set(kKeyCookie, cookie);
    Config::instance().save();
}

// ---------------- 对外 ----------------
const QString &Fingerprint::cookie() {
    // 注意: 必须用可重入锁 — 采集过程自身会发 HTTP 请求, 请求路径又会回到本方法
    // (HttpClient::execSync 合并 Cookie 时调用), 非递归锁将死锁
    static QRecursiveMutex s_mutex;
    QMutexLocker locker(&s_mutex);

    if (!m_tried) {
        m_tried = true;
        if (!loadFromConfig()) {
            Logger::info("指纹 Cookie: 本地无有效缓存, 开始联网采集...");
            m_cookie = acquire();
            if (m_cookie.isEmpty()) {
                Logger::warn("指纹 Cookie: 采集失败, 本次请求将不带指纹 (可能触发风控)");
            } else {
                saveToConfig(m_cookie);
                Logger::info("指纹 Cookie: 采集成功 (长度 " + QString::number(m_cookie.size()) + ")");
            }
        } else {
            Logger::info("指纹 Cookie: 使用本地缓存");
        }
    }
    return m_cookie;
}

void Fingerprint::reset() {
    static QRecursiveMutex s_mutex;
    QMutexLocker locker(&s_mutex);
    m_cookie.clear();
    m_tried = false;
    Config::instance().set(kKeyCookie, QString());
    Config::instance().save();
}

} // namespace bili::api
