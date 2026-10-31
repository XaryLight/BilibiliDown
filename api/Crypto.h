#ifndef BILI_API_CRYPTO_H
#define BILI_API_CRYPTO_H

#include <QString>
#include <QByteArray>
#include <cstdint>

namespace bili::api {

/**
 * @brief 编解码/摘要工具集
 *         - MD5 / SHA1 摘要 (WBI 签名 / ffmpeg 完整性校验)
 *         - AV <-> BV 号互转 (参考 Colerar/abv, MIT)
 *         - WBI 签名 (B站 web 接口防风控)
 *         - URL 编解码
 */
class Crypto {
public:
    static QByteArray md5(const QByteArray &data);
    static QString md5Hex(const QByteArray &data);
    static QString sha1Hex(const QByteArray &data);

    // AV <-> BV
    static int64_t bv2av(const QString &bvId);
    static QString av2bv(int64_t avNum);
    static QString av2bv(const QString &avStr); // "av12345" / "12345"

    // URL 编解码 (与 Java URLEncoder 行为对齐: 空格 -> %20)
    static QString urlEncode(const QString &raw);
    static QString urlEncodeValue(const QString &value);

    // WBI 签名: 获取 mixin key 并生成最终 url (带 w_rid 与 wts)
    // navJson: https://api.bilibili.com/x/web-interface/nav 返回的原始报文
    static QString wbiSign(const QString &url, const QString &navJson);

    // 从 nav 报文中提取并拼接 img_key + sub_key (内部使用)
    static QString extractWbiImgKey(const QString &navJson);

    // 常用: 请求 nav 并签名 (同步网络, 内部调用 HttpClient)
    static QString wbiSignAuto(const QString &url);

    // 生成 dm_img 系列参数 (风控指纹)
    static QString genDmImgParams(const QString &userAgentPayload = QString());

private:
    static QString getMixinKey(const QString &imgKey);
    static QString sortAndEncodeParams(const QString &query);
};

} // namespace bili::api

#endif // BILI_API_CRYPTO_H
