#ifndef BILI_CODE_PARSER_ABSTRACT_BASE_PARSER_H
#define BILI_CODE_PARSER_ABSTRACT_BASE_PARSER_H

#include "model/Types.h"

#include <api/HttpClient.h>
#include <api/Logger.h>
#include <api/Crypto.h>

#include <QString>
#include <QList>
#include <QJsonObject>
#include <memory>

namespace bili::parser {

/**
 * @brief 解析器异常
 */
class BiliException : public std::exception {
public:
    explicit BiliException(QString msg) : m_msg(std::move(msg)), m_what(m_msg.toUtf8()) {}
    // 注意: m_what 必须在 m_msg 之后声明, 保证初始化时 m_msg 已就绪;
    // 且 what() 不能返回临时 QByteArray 的 constData() (临时对象即析构, 指针悬空)
    const char *what() const noexcept override { return m_what.constData(); }
    QString message() const { return m_msg; }
private:
    QString m_msg;
    QByteArray m_what;
};

/**
 * @brief 所有 URL 解析器的基类 (对应 Java 版 AbstractBaseParser)
 *
 *  职责:
 *   - 从用户输入 (url/av/bv/ss/ep/ml/au...) 解析出 VideoInfo (含 clips)
 *   - 查询每个 clip 的下载链接 (playurl API, 支持 DASH m4s 与 FLV)
 *   - WBI 签名 / 风控参数
 */
class AbstractBaseParser {
public:
    virtual ~AbstractBaseParser() = default;

    // 输入是否匹配本解析器
    virtual bool matches(const QString &input) = 0;

    // 规范化后的输入 (用于显示)
    virtual QString validStr(const QString &input) = 0;

    // 解析并 (可选) 查询下载链接
    // videoFormat: 0 MP4(DASH) / 1 FLV / 2 仅音频
    // getVideoLink: 是否查询每个清晰度的下载链接 (false 时仅获取列表)
    virtual model::VideoInfo result(const QString &input, int videoFormat, bool queryLink) = 0;

    // ---- 通用能力 (子类复用) ----
    // 获取 AV/BV 下的分P列表与详细信息
    model::VideoInfo getAVDetail(const QString &bvId, int videoFormat, bool queryLink);

    // 按 bvid (+1-based 分P号, 默认第1P) 查询 cid
    // 用于解析期拿不到 cid 的场景 (如 UP主空间列表 vlist 无 cid 字段), 下载时懒加载
    int64_t resolveCid(const QString &bvId, int page = 1);

    // 查询清晰度列表
    QList<int> getVideoQNList(const QString &bvId, const QString &cid);

    // 查询下载链接 (对外入口)
    // 返回: "videoUrl#audioUrl" (DASH) / "order1url1#order2url2..." (FLV) / "url" (单段)
    QString getVideoLink(const QString &bvId, const QString &cid, int qn, int downFormat);

    // 当前实际清晰度 (查询链接后设置)
    int realQN() const { return m_realQN; }

protected:
    explicit AbstractBaseParser(int pageSize = 20);

    // HTTP 便捷方法
    QString httpGet(const QString &url, const api::Headers &headers);
    QString httpGet(const QString &url); // 默认 json api 头

    // ---- 链接查询内部实现 ----
    QString getVideoLinkByFormat(const QString &bvId, const QString &cid, int qn, int downloadFormat);
    QString getAudioLink(const QString &auId, int qn);
    QString getVideoSubtitleLink(const QString &bvId, const QString &cid);

    // DASH 解析: 从 playurl 报文中挑选视频/音频链接
    // linkQN 为实际返回的清晰度
    QString parseDash(const QJsonObject &data, int &linkQN);

    // FLV 解析 (durl)
    QString parseFlv(const QJsonObject &data);

    // 从 media 列表中选择指定 codecid/id 的条目, 并做连通性检测
    QString urlOfMedia(const QJsonObject &media, const api::Headers &checkHeader);

    // 构造带 dm_img 参数 + WBI 签名的 url
    QString wbiUrl(const QString &url);

    int m_pageSize = 20;
    int m_realQN = 0;
};

} // namespace bili::parser

#endif // BILI_CODE_PARSER_ABSTRACT_BASE_PARSER_H
