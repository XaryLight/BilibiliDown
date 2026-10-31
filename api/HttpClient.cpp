#include "HttpClient.h"
#include "Logger.h"
#include "Config.h"
#include "Fingerprint.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QUrlQuery>
#include <QDebug>

namespace bili::api {

HttpClient &HttpClient::instance() {
    static HttpClient s_instance;
    return s_instance;
}

// ---------------- Cookie 管理 ----------------
static QMutex s_cookieMutex;
static QString s_globalCookie;

QString HttpClient::globalCookie() {
    QMutexLocker l(&s_cookieMutex);
    return s_globalCookie;
}

void HttpClient::setGlobalCookie(const QString &cookie) {
    QMutexLocker l(&s_cookieMutex);
    s_globalCookie = cookie;
}

// ---------------- 请求头 ----------------
Headers HttpClient::defaultHeaders(const QString &referer) {
    Headers h;
    h["User-Agent"] = Config::instance().userAgent();
    h["Accept"] = "*/*";
    h["Accept-Language"] = "zh-CN,zh;q=0.9";
    h["Connection"] = "keep-alive";
    if (!referer.isEmpty())
        h["Referer"] = referer;
    else
        h["Referer"] = "https://www.bilibili.com/";
    return h;
}

Headers HttpClient::jsonApiHeaders(const QString &referer) {
    Headers h = defaultHeaders(referer.isEmpty() ? "https://www.bilibili.com/" : referer);
    h["Accept"] = "application/json, text/plain, */*";
    return h;
}

Headers HttpClient::m4sHeaders(const QString &referer) {
    Headers h = defaultHeaders(referer);
    h["Origin"] = "https://www.bilibili.com";
    return h;
}

// ---------------- 同步执行 ----------------
HttpClient::Result HttpClient::execSync(QNetworkRequest &req, int timeoutMs) {
    Result r;
    QNetworkAccessManager nam;
    // 合并用户 Cookie 与浏览器指纹 Cookie (buvid3 等, 防风控 412)
    // 指纹采集自身也会走本方法, 用 thread_local 守卫避免递归
    QString cookie = globalCookie();
    thread_local bool t_inFingerprint = false;
    if (!t_inFingerprint) {
        struct Guard {
            bool &flag;
            ~Guard() { flag = false; }
        } guard{t_inFingerprint};
        t_inFingerprint = true;
        const QString &fp = Fingerprint::instance().cookie();
        if (!fp.isEmpty())
            cookie = cookie.isEmpty() ? fp : (fp + "; " + cookie);
    }
    if (!cookie.isEmpty())
        req.setRawHeader("Cookie", cookie.toUtf8());

    QNetworkReply *reply = nam.get(req);
    if (!reply) {
        r.error = "无法创建请求";
        return r;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    // POST 复用同一个 loop: 上面的 get 版本仅用于 GET;
    // POST 走单独分支 (见 postSync)
    timer.start(timeoutMs);
    loop.exec();

    if (!timer.isActive()) {
        r.error = QString("请求超时: %1").arg(req.url().toString());
        reply->deleteLater();
        return r;
    }
    timer.stop();

    if (reply->error() != QNetworkReply::NoError) {
        // 超时 abort 也会到这里
        r.error = reply->errorString();
        r.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // 即使有错误, 也尽量读取已到达的数据 (部分错误场景下 body 有用)
        r.body = reply->readAll();
        reply->deleteLater();
        return r;
    }

    r.ok = true;
    r.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    r.body = reply->readAll();
    r.rawHeaders = reply->rawHeaderPairs();
    r.finalUrl = reply->url();
    reply->deleteLater();
    return r;
}

HttpClient::Result HttpClient::getSync(const QString &url, const Headers &headers) {
    QNetworkRequest req{QUrl(url)};
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    Result r = execSync(req);
    if (r.ok) {
        Logger::debug(QString("GET %1 -> %2 (%3 bytes)")
                          .arg(url).arg(r.statusCode).arg(r.body.size()));
    } else {
        Logger::warn(QString("GET %1 失败: %2").arg(url, r.error));
    }
    return r;
}

HttpClient::Result HttpClient::postSync(const QString &url, const QByteArray &body,
                                        const Headers &headers, const QString &contentType) {
    Result r;
    QNetworkRequest req{QUrl(url)};
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkAccessManager nam;
    QString cookie = globalCookie();
    if (!cookie.isEmpty())
        req.setRawHeader("Cookie", cookie.toUtf8());

    QNetworkReply *reply = nam.post(req, body);
    if (!reply) {
        r.error = "无法创建请求";
        return r;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() { reply->abort(); });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(30000);
    loop.exec();

    if (!timer.isActive()) {
        r.error = "请求超时";
        reply->deleteLater();
        return r;
    }
    timer.stop();

    if (reply->error() != QNetworkReply::NoError) {
        r.error = reply->errorString();
        r.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.body = reply->readAll();
        r.rawHeaders = reply->rawHeaderPairs();
        reply->deleteLater();
        return r;
    }
    r.ok = true;
    r.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    r.body = reply->readAll();
    r.rawHeaders = reply->rawHeaderPairs();
    r.finalUrl = reply->url();
    reply->deleteLater();
    Logger::debug(QString("POST %1 -> %2 (%3 bytes)").arg(url).arg(r.statusCode).arg(r.body.size()));
    return r;
}

// ---------------- 文件下载 (Range 续传) ----------------
bool HttpClient::downloadFile(const QString &url, const QString &destPath,
                              const Headers &headers,
                              ProgressCallback progress,
                              const std::function<bool()> &isCancelled,
                              QString *errorOut) {
    auto fail = [&](const QString &msg) {
        if (errorOut) *errorOut = msg;
        Logger::error(QString("下载失败 %1 : %2").arg(url, msg));
        return false;
    };

    const QString partPath = destPath + ".part";
    QFile partFile(partPath);
    qint64 offset = 0;
    if (partFile.exists()) {
        if (!partFile.open(QIODevice::Append)) {
            return fail(QString("无法打开临时文件: %1").arg(partPath));
        }
        offset = partFile.size();
        if (offset > 0) {
            Logger::info(QString("断点续传 %1 已下载 %2 字节").arg(QFileInfo(partPath).fileName()).arg(offset));
        }
    } else {
        if (!partFile.open(QIODevice::WriteOnly)) {
            return fail(QString("无法创建临时文件: %1").arg(partPath));
        }
    }

    QNetworkRequest req{QUrl(url)};
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    if (offset > 0)
        req.setRawHeader("Range", QString("bytes=%1-").arg(offset).toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);

    QNetworkAccessManager nam;
    QString cookie = globalCookie();
    if (!cookie.isEmpty())
        req.setRawHeader("Cookie", cookie.toUtf8());

    QNetworkReply *reply = nam.get(req);
    if (!reply) {
        partFile.close();
        return fail("无法创建请求");
    }

    // 读取循环: 使用事件驱动逐段写入
    qint64 total = 0;
    {
        // 先等待 header 到达以获知 Content-Length
        QEventLoop hdrLoop;
        QObject::connect(reply, &QNetworkReply::finished, &hdrLoop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::metaDataChanged, &hdrLoop, [&]() {
            // 状态码已知即认为头部就绪
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid())
                hdrLoop.quit();
        });
        hdrLoop.exec();
        if (reply->error() != QNetworkReply::NoError) {
            QString err = reply->errorString();
            reply->deleteLater();
            partFile.close();
            return fail(err);
        }
        bool ok = false;
        qint64 contentLength = reply->rawHeader("Content-Length").toLongLong(&ok);
        if (ok) total = contentLength;
    }

    qint64 received = offset;
    Progress p{received, total + offset};
    if (progress) progress(p);

    QEventLoop loop;
    bool aborted = false;
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&]() {
        QByteArray chunk = reply->readAll();
        if (!chunk.isEmpty()) {
            partFile.write(chunk);
            received += chunk.size();
            if (progress) {
                Progress pp{received, total + offset};
                progress(pp);
            }
        }
        if (isCancelled && isCancelled()) {
            aborted = true;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop,
                     [&](qint64 bytesReceived, qint64 bytesTotal) {
                         if (progress) {
                             Progress pp{offset + bytesReceived, offset + bytesTotal};
                             progress(pp);
                         }
                     }, Qt::QueuedConnection);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (aborted) {
        partFile.close();
        reply->deleteLater();
        if (errorOut) *errorOut = "已取消";
        Logger::info(QString("下载取消: %1").arg(url));
        return false;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QString err = reply->errorString();
        partFile.close();
        reply->deleteLater();
        return fail(err);
    }

    // flush 剩余数据
    {
        QByteArray chunk = reply->readAll();
        if (!chunk.isEmpty()) partFile.write(chunk);
    }
    partFile.close();
    reply->deleteLater();

    // 校验完整性
    if (total > 0) {
        qint64 expect = total + offset;
        if (QFileInfo(partPath).size() < expect) {
            // 允许重试: 保留 .part 供下次续传
            return fail(QString("文件不完整: %1/%2").arg(QFileInfo(partPath).size()).arg(expect));
        }
    }

    // 完成: 移除旧目标, 重命名 .part -> 目标
    QFile::remove(destPath);
    if (!partFile.rename(destPath)) {
        // rename 失败时尝试 copy+remove
        if (!partFile.copy(destPath)) {
            return fail("重命名临时文件失败");
        }
        partFile.remove();
    }
    Logger::info(QString("下载完成: %1 (%2 bytes)").arg(QFileInfo(destPath).fileName()).arg(QFileInfo(destPath).size()));
    return true;
}

bool HttpClient::checkValid(const QString &url, const Headers &headers) {
    QNetworkRequest req{QUrl(url)};
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    req.setRawHeader("Range", "bytes=0-100");
    QNetworkAccessManager nam;
    QNetworkReply *reply = nam.head(req);
    if (!reply) return false;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() { reply->abort(); });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(10000);
    loop.exec();
    bool valid = false;
    if (timer.isActive()) {
        timer.stop();
        int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        valid = (code != 404 && reply->error() == QNetworkReply::NoError) ||
                (code >= 200 && code < 500 && code != 404);
    }
    reply->deleteLater();
    return valid;
}

} // namespace bili::api
