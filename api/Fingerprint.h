#ifndef BILI_API_FINGERPRINT_H
#define BILI_API_FINGERPRINT_H

#include <QString>

namespace bili::api {

/**
 * @brief 浏览器指纹 Cookie (buvid3/buvid4 等)
 *
 *  B站 WAF 对无 Cookie 的客户端极易返回 HTTP 412 (风控), 真实浏览器始终带有
 *  buvid3/buvid4 等指纹 Cookie. 本模块模拟该行为:
 *   - 首次: GET https://www.bilibili.com/ 收集 Set-Cookie
 *           再 GET x/frontend/finger/spi 获取 buvid4
 *   - 持久化到 QSettings, b_lsid 定期刷新, b_nut 一年有效
 *   - HttpClient 请求时自动合并 (见 HttpClient::execSync)
 *
 *  线程安全: 内部加锁; 采集过程不走本客户端的 Cookie 合并逻辑 (无递归).
 */
class Fingerprint {
public:
    static Fingerprint &instance();

    // 返回指纹 Cookie 字符串 (形如 "buvid3=xx; buvid4=yy; ..."); 采集失败返回空
    // 首次调用会联网采集 (阻塞, 请在worker线程或可容忍阻塞处调用)
    const QString &cookie();

    // 强制重新采集 (当前 Cookie 被风控拒绝时可调用)
    void reset();

private:
    Fingerprint() = default;
    QString acquire();        // 联网采集, 返回 Cookie 串 (失败为空)
    bool loadFromConfig();    // 从配置加载, 含过期/b_lsid 刷新处理
    void saveToConfig(const QString &cookie);

    QString m_cookie;
    bool m_tried = false;
};

} // namespace bili::api

#endif // BILI_API_FINGERPRINT_H
