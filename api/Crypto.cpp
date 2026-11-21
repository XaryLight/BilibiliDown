#include "Crypto.h"
#include "Logger.h"
#include "HttpClient.h"

#include <QCryptographicHash>
#include <QUrlQuery>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QMutex>
#include <QFile>
#include <cstring>

namespace bili::api {

// ---------------- 摘要 ----------------
QByteArray Crypto::md5(const QByteArray &data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Md5);
}

QString Crypto::md5Hex(const QByteArray &data) {
    return QString(md5(data).toHex());
}

QString Crypto::sha1Hex(const QByteArray &data) {
    return QString(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
}

QString Crypto::fileSha256(const QString &filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) {
        f.close();
        return QString();
    }
    f.close();
    return QString::fromLatin1(hash.result().toHex());
}

// ---------------- AV <-> BV ----------------
static const char *kTable = "FcwAPNKTMug3GV5Lj7EJnHpWsx4tb8haYeviqBz6rkCy12mUSDQX9RdoZf";
static constexpr int64_t kXor = 23442827791579LL;
static constexpr int64_t kMask = 2251799813685247LL;
static constexpr int64_t kMaxAid = 1LL << 51;

int64_t Crypto::bv2av(const QString &bvId) {
    QByteArray bv = bvId.toUtf8();
    if (bv.size() < 12) return 0;
    std::swap(bv[3], bv[9]);
    std::swap(bv[4], bv[7]);
    int64_t tmp = 0;
    for (int i = 3; i < bv.size(); ++i) {
        const char *pos = std::strchr(kTable, bv[i]);
        if (!pos) return 0;
        int tableIdx = static_cast<int>(pos - kTable);
        tmp = tmp * 58 + tableIdx;
    }
    return (tmp & kMask) ^ kXor;
}

QString Crypto::av2bv(int64_t avNum) {
    QByteArray bv = "BV1000000000";
    int bvIdx = 11;
    int64_t tmp = (kMaxAid | avNum) ^ kXor;
    while (tmp != 0) {
        int tableIdx = static_cast<int>(tmp % 58);
        bv[bvIdx] = kTable[tableIdx];
        tmp /= 58;
        bvIdx--;
    }
    std::swap(bv[3], bv[9]);
    std::swap(bv[4], bv[7]);
    return QString::fromUtf8(bv);
}

QString Crypto::av2bv(const QString &avStr) {
    QString num = avStr;
    num.remove("av", Qt::CaseInsensitive);
    bool ok = false;
    int64_t v = num.toLongLong(&ok);
    if (!ok) return QString();
    return av2bv(v);
}

// ---------------- URL 编解码 ----------------
QString Crypto::urlEncode(const QString &raw) {
    QString s = QUrl::toPercentEncoding(raw);
    return s;
}

QString Crypto::urlEncodeValue(const QString &value) {
    // 与 Java encodeURL 行为一致: 已含 % 则不编码, + 转为 %20
    if (value.contains('%'))
        return value;
    return QUrl::toPercentEncoding(value).replace('+', "%20");
}

// ---------------- WBI 签名 ----------------
static const int kMixinArray[] = {
    46, 47, 18, 2, 53, 8, 23, 32, 15, 50, 10, 31, 58, 3, 45, 35, 27, 43, 5, 49,
    33, 9, 42, 19, 29, 28, 14, 39, 12, 38, 41, 13, 37, 48, 7, 16, 24, 55, 40, 61,
    26, 17, 0, 1, 60, 51, 30, 4, 22, 25, 54, 21, 56, 59, 6, 63, 57, 62, 11, 36,
    20, 34, 44, 52};

QString Crypto::getMixinKey(const QString &imgKey) {
    QString sb;
    sb.reserve(32);
    for (int i = 0; i < 32; ++i)
        sb.append(imgKey.at(kMixinArray[i]));
    return sb;
}

QString Crypto::extractWbiImgKey(const QString &navJson) {
    QJsonDocument doc = QJsonDocument::fromJson(navJson.toUtf8());
    if (doc.isNull()) return QString();
    QJsonObject wbi = doc.object().value("data").toObject().value("wbi_img").toObject();
    QString imgUrl = wbi.value("img_url").toString();
    QString subUrl = wbi.value("sub_url").toString();
    if (imgUrl.isEmpty() || subUrl.isEmpty()) return QString();

    auto keyOf = [](const QString &u) {
        int is = u.lastIndexOf('/');
        int ie = u.indexOf('.', is);
        if (is < 0 || ie < 0) return QString();
        return u.mid(is + 1, ie - is - 1);
    };
    return keyOf(imgUrl) + keyOf(subUrl);
}

QString Crypto::sortAndEncodeParams(const QString &query) {
    // query: k=v&k2=v2 ... 逐项编码并排序
    QStringList params = query.split('&', Qt::SkipEmptyParts);
    QStringList encoded;
    encoded.reserve(params.size());
    for (const QString &kv : params) {
        int eq = kv.indexOf('=');
        QString key, value;
        if (eq >= 0) {
            key = kv.left(eq);
            value = kv.mid(eq + 1);
        } else {
            key = kv;
        }
        encoded << QUrl::toPercentEncoding(key) + "=" + urlEncodeValue(value);
    }
    encoded.sort();
    return encoded.join("&");
}

QString Crypto::wbiSign(const QString &url, const QString &navJson) {
    QString imgKey = extractWbiImgKey(navJson);
    if (imgKey.isEmpty()) {
        Logger::warn("WBI 签名: 无法从 nav 报文提取 img_key");
        return url;
    }
    QString mixinKey = getMixinKey(imgKey);

    int qmIdx = url.indexOf('?');
    QString paramRaw = qmIdx >= 0 ? url.mid(qmIdx + 1) : QString();
    QString wts = QString("wts=%1").arg(QDateTime::currentSecsSinceEpoch());

    QString paramEncodedSorted;
    QString sep;
    if (qmIdx >= 0) {
        if (paramRaw.isEmpty()) sep = "";
        else sep = "&";
        paramRaw += sep + wts;
        paramEncodedSorted = sortAndEncodeParams(paramRaw);
    } else {
        sep = "?";
        paramEncodedSorted = wts;
    }

    QString md5 = md5Hex((paramEncodedSorted + mixinKey).toUtf8());
    // 注意: 与 Java 版一致, 使用完整原始 url 作为前缀 (含 ? 及其后参数)
    return QString("%1%2w_rid=%3&%4").arg(url, sep, md5, wts);
}

QString Crypto::wbiSignAuto(const QString &url) {
    HttpClient &http = HttpClient::instance();
    // img_key 缓存 10 分钟: B站轮换不频繁, 避免每次签名都请求 nav (请求量减半, 降低风控概率)
    static QMutex s_mutex;
    static QString s_navJson;
    static qint64 s_fetchMs = 0;
    const qint64 kTtlMs = 10 * 60 * 1000;

    QMutexLocker locker(&s_mutex);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (s_navJson.isEmpty() || now - s_fetchMs > kTtlMs) {
        auto r = http.getSync("https://api.bilibili.com/x/web-interface/nav",
                              HttpClient::jsonApiHeaders());
        if (r.ok) {
            QString navJson = QString::fromUtf8(r.body);
            if (!extractWbiImgKey(navJson).isEmpty()) {
                s_navJson = navJson;
                s_fetchMs = now;
            } else {
                Logger::warn("WBI 签名: nav 报文中无 wbi_img");
            }
        } else {
            Logger::warn(QString("WBI 签名: nav 请求失败 %1").arg(r.error));
        }
    }
    if (s_navJson.isEmpty())
        return url; // 拿不到 key 时退回原样 (部分接口不校验签名)
    return wbiSign(url, s_navJson);
}

// dm_img 系列参数 (浏览器风控指纹)
static QString b64Sub2(const QString &data) {
    QByteArray b = data.toUtf8().toBase64();
    if (b.size() >= 2) b.chop(2);
    return QString::fromUtf8(b);
}

// 实际生成逻辑 (无缓存)
static QString genDmImgParamsUncached() {
    // 与 Java 版一致: 从 userAgentPayload 提取 webgl/resolution/renderer 等信息
    QString dmImgList = "[]";
    QString webglVersion = "WebGL 1.0 (OpenGL ES 2.0 Chromium)";
    int width = 1920, height = 1080;
    QString renderer = "ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0)";
    QString vendor = "Google Inc. (NVIDIA)";

    auto f114 = [](int a, int b) {
        int t = QRandomGenerator::global()->bounded(114);
        return QString("[%1,%2,%3]").arg(2 * a + 2 * b + 3 * t).arg(4 * a - b + t).arg(t);
    };
    auto f514 = [](int a, int b) {
        int t = QRandomGenerator::global()->bounded(514);
        return QString("[%1,%2,%3]").arg(3 * a + 2 * b + t).arg(4 * a - 4 * b + 2 * t).arg(t);
    };

    QString dmImgStr = b64Sub2(webglVersion);
    QString dmImgInter = QString(R"({"ds":[],"wh":%1,"of":%2})").arg(f114(width, height), f514(0, 0));
    QString dmCoverImgStr = b64Sub2(renderer + vendor);

    return QString("&dm_img_list=%1&dm_img_str=%2&dm_img_inter=%3&dm_cover_img_str=%4")
        .arg(QString(QUrl::toPercentEncoding(dmImgList)),
             QString(QUrl::toPercentEncoding(dmImgStr)),
             QString(QUrl::toPercentEncoding(dmImgInter)),
             QString(QUrl::toPercentEncoding(dmCoverImgStr)));
}

QString Crypto::genDmImgParams(const QString &payload) {
    if (!payload.isEmpty())
        return genDmImgParamsUncached(); // 指定 payload 时按输入生成 (当前无调用方使用)
    // 进程内只生成一次: 每次请求都换随机值反而更像机器人, 浏览器指纹是稳定的
    static const QString s_cached = genDmImgParamsUncached();
    return s_cached;
}

} // namespace bili::api
