#ifndef BILI_CODE_UTIL_LOGIN_CLIENT_H
#define BILI_CODE_UTIL_LOGIN_CLIENT_H

#include <QObject>
#include <QString>
#include <QImage>
#include <QNetworkReply>

namespace bili::util {

/**
 * @brief B站登录客户端 (二维码扫码登录)
 *
 *  流程:
 *   1. generateQr()      -> 获取二维码内容 url + qrcode_key
 *   2. 界面显示二维码
 *   3. poll()            -> 轮询扫码状态 (待扫描/已扫描待确认/成功/过期)
 *   4. 成功时从响应头 Set-Cookie 提取登录凭据
 */
class LoginClient : public QObject {
    Q_OBJECT
public:
    enum class PollStatus {
        WaitScan,      // 等待扫码
        WaitConfirm,   // 已扫码, 等待确认
        Success,       // 登录成功
        Expired,       // 二维码过期
        Failed         // 失败
    };

    explicit LoginClient(QObject *parent = nullptr);

    // 生成登录二维码, 返回二维码图片 (失败返回空 QImage)
    // key 输出 qrcode_key
    QImage generateQr(QString &key, QString &errorMsg);

    // 轮询扫码状态; 成功时 cookie 输出完整 cookie 字符串
    PollStatus poll(const QString &key, QString &cookie, QString &message);

    // 退出登录
    bool logout(const QString &cookie);

    // 从 Set-Cookie 响应头列表中提取需要的 cookie
    static QString extractCookies(const QList<QNetworkReply::RawHeaderPair> &headers);

private:
    QString m_loginUrl;
};

} // namespace bili::util

#endif // BILI_CODE_UTIL_LOGIN_CLIENT_H
