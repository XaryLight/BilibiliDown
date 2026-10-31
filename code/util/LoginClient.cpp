#include "LoginClient.h"
#include <api/HttpClient.h>
#include <api/Crypto.h>
#include <api/Logger.h>
#include <api/QrCode.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkReply>

namespace bili::util {

using bili::api::HttpClient;
using bili::api::Logger;

LoginClient::LoginClient(QObject *parent) : QObject(parent) {}

QImage LoginClient::generateQr(QString &key, QString &errorMsg) {
    // 2023 年后旧接口 /qrcode/generate 已下线(返回404), 改用 passport-login 新接口
    QString url = "https://passport.bilibili.com/x/passport-login/web/qrcode/generate";
    url += "?source=main-web";
    auto r = HttpClient::instance().getSync(url, HttpClient::defaultHeaders("https://passport.bilibili.com/login"));
    if (!r.ok) {
        errorMsg = "网络请求失败: " + r.error;
        return QImage();
    }
    QJsonObject obj = QJsonDocument::fromJson(r.body).object();
    if (obj.value("code").toInt() != 0) {
        errorMsg = "生成二维码失败: " + obj.value("message").toString();
        return QImage();
    }
    QJsonObject data = obj.value("data").toObject();
    QString qrUrl = data.value("url").toString();
    key = data.value("qrcode_key").toString();
    if (qrUrl.isEmpty() || key.isEmpty()) {
        errorMsg = "二维码数据异常";
        return QImage();
    }
    m_loginUrl = qrUrl;
    Logger::info("登录二维码已生成, key=" + key);
    return bili::api::QrCode::generate(qrUrl, bili::api::QrCode::Ecc::Medium, 1, 10, 6, 3);
}

LoginClient::PollStatus LoginClient::poll(const QString &key, QString &cookie, QString &message) {
    // 与 generate 对应的新轮询接口 (旧 /qrcode/poll 已一并下线)
    QString url = QString("https://passport.bilibili.com/x/passport-login/web/qrcode/poll"
                          "?source=main-web&qrcode_key=%1").arg(key);
    auto r = HttpClient::instance().getSync(url, HttpClient::defaultHeaders("https://passport.bilibili.com/login"));
    if (!r.ok) {
        message = "轮询失败: " + r.error;
        return PollStatus::Failed;
    }
    QJsonObject obj = QJsonDocument::fromJson(r.body).object();
    QJsonObject data = obj.value("data").toObject();
    int code = data.value("code").toInt();
    switch (code) {
        case 86101: // 未扫描
            message = "请使用哔哩哔哩客户端扫码";
            return PollStatus::WaitScan;
        case 86090: // 已扫描未确认
            message = "已扫描, 请在手机上确认";
            return PollStatus::WaitConfirm;
        case 86038: // 过期
            message = "二维码已过期, 请刷新";
            return PollStatus::Expired;
        case 0: {  // 成功
            QString c = extractCookies(r.rawHeaders);
            if (c.isEmpty()) {
                message = "登录成功但未获取到 cookie";
                return PollStatus::Failed;
            }
            cookie = c;
            message = "登录成功";
            Logger::info("二维码登录成功, cookie 长度: " + QString::number(c.size()));
            return PollStatus::Success;
        }
        default:
            message = obj.value("message").toString();
            return PollStatus::Failed;
    }
}

bool LoginClient::logout(const QString &cookie) {
    QUrl url("https://passport.bilibili.com/login/exit/v2");
    QUrlQuery params;
    params.addQueryItem("biliCSRF", [&cookie]() {
        // csrf = bili_jct cookie 值
        for (const QString &kv : cookie.split(';')) {
            QString s = kv.trimmed();
            if (s.startsWith("bili_jct="))
                return s.mid(9);
        }
        return QString();
    }());
    url.setQuery(params);
    api::Headers h = HttpClient::defaultHeaders("https://www.bilibili.com/");
    h["Cookie"] = cookie;
    auto r = HttpClient::instance().postSync(url.toString(), QByteArray(), h,
                                             "application/x-www-form-urlencoded");
    return r.ok;
}

QString LoginClient::extractCookies(const QList<QNetworkReply::RawHeaderPair> &headers) {
    // 需要的登录 cookie
    static const QStringList kNeed = {
        "SESSDATA", "bili_jct", "DedeUserID", "DedeUserID__ckMd5", "sid"
    };
    QHash<QString, QString> found;
    for (const auto &hp : headers) {
        if (QString::fromUtf8(hp.first).compare("Set-Cookie", Qt::CaseInsensitive) != 0)
            continue;
        QString value = QString::fromUtf8(hp.second);
        // 形如: SESSDATA=xxx; path=/; domain=.bilibili.com; ...
        QString first = value.section(';', 0, 0);
        int eq = first.indexOf('=');
        if (eq <= 0) continue;
        QString name = first.left(eq).trimmed();
        QString val = first.mid(eq + 1).trimmed();
        if (kNeed.contains(name))
            found.insert(name, val);
    }
    if (!found.contains("SESSDATA"))
        return QString();
    QStringList result;
    for (const QString &name : kNeed) {
        if (found.contains(name))
            result << name + "=" + found.value(name);
    }
    return result.join("; ");
}

} // namespace bili::util
