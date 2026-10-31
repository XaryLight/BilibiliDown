#ifndef BILI_API_HTTP_CLIENT_H
#define BILI_API_HTTP_CLIENT_H

#include <QString>
#include <QByteArray>
#include <QUrl>
#include <QHash>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <functional>

namespace bili::api {

using Headers = QHash<QString, QString>;

/**
 * @brief HTTP 客户端 (Qt Network 封装)
 *
 *  - 同步 getSync / postSync: 内部使用 QEventLoop 等待, 可在任意线程调用
 *  - downloadFile: 支持 Range 断点续传 (.part 临时文件), 进度回调, 取消标志
 *  - Cookie: 以字符串形式管理, 请求时拼接为 Cookie 头
 *
 *  线程安全: 每次调用内部创建独立的 QNetworkAccessManager,
 *  不同线程可并发使用本客户端.
 */
class HttpClient {
public:
    struct Result {
        bool ok = false;
        int statusCode = 0;
        QByteArray body;
        QString error;
        QUrl finalUrl;   // 重定向后的最终 URL
        QList<QNetworkReply::RawHeaderPair> rawHeaders;
    };

    struct Progress {
        qint64 received = 0;
        qint64 total = 0;
    };
    using ProgressCallback = std::function<void(const Progress &)>;

    static HttpClient &instance();

    // 同步 GET / POST
    Result getSync(const QString &url, const Headers &headers = {});
    Result postSync(const QString &url, const QByteArray &body,
                    const Headers &headers = {}, const QString &contentType = "application/x-www-form-urlencoded");

    // 下载文件到 destPath (自动 .part 临时 + Range 续传)
    // cancel 置 true 时中止; 返回 false 且 error 非空表示失败
    bool downloadFile(const QString &url, const QString &destPath,
                      const Headers &headers = {},
                      ProgressCallback progress = nullptr,
                      const std::function<bool()> &isCancelled = nullptr,
                      QString *errorOut = nullptr);

    // 探测 URL 有效性 (HEAD 或小 Range GET), 供备用 url 选择
    bool checkValid(const QString &url, const Headers &headers = {});

    // 全局 Cookie 管理 (形如 "k1=v1; k2=v2")
    static QString globalCookie();
    static void setGlobalCookie(const QString &cookie);

    // 构造默认请求头
    static Headers defaultHeaders(const QString &referer = QString());
    static Headers jsonApiHeaders(const QString &referer = QString());
    static Headers m4sHeaders(const QString &referer = QString());

private:
    HttpClient() = default;
    Result execSync(QNetworkRequest &req, int timeoutMs = 30000);
};

} // namespace bili::api

#endif // BILI_API_HTTP_CLIENT_H
