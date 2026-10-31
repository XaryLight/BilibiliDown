#ifndef BILI_CODE_PARSER_IMPL_PARSERS_H
#define BILI_CODE_PARSER_IMPL_PARSERS_H

#include "AbstractBaseParser.h"

#include <QRegularExpression>

namespace bili::parser {

/**
 * @brief BV/AV/链接解析器 (兜底)
 *  匹配: BV1xx411c7mD / av123456 / b23.tv 短链 / 含 BV 的完整 url
 */
class BvParser : public AbstractBaseParser {
public:
    explicit BvParser(int pageSize = 20) : AbstractBaseParser(pageSize) {}

    bool matches(const QString &input) override {
        static const QRegularExpression re(
            R"RE((?:b23\.tv\/|BV1[a-zA-Z0-9]{9}|av\d{3,}))RE");
        return re.match(input).hasMatch();
    }

    QString validStr(const QString &input) override {
        static const QRegularExpression bvRe(R"RE(BV1[a-zA-Z0-9]{9})RE");
        auto m = bvRe.match(input);
        if (m.hasMatch()) return m.captured(0);
        return input;
    }

    model::VideoInfo result(const QString &input, int videoFormat, bool queryLink) override;

private:
    QString resolveB23(const QString &shortUrl);
};

/**
 * @brief 番剧解析器 (ss/md)
 *  匹配: ss12345 / md134912 / bangumi/play/ss12345 / bangumi/media/md134912
 */
class SSParser : public AbstractBaseParser {
public:
    explicit SSParser(int pageSize = 20) : AbstractBaseParser(pageSize) {}

    bool matches(const QString &input) override;
    QString validStr(const QString &input) override;
    model::VideoInfo result(const QString &input, int videoFormat, bool queryLink) override;

private:
    QString m_ssId;
    model::VideoInfo getSSDetail(const QString &ssId, int videoFormat, bool queryLink);
    QString mdIdToSsId(const QString &mdId);
};

/**
 * @brief 番剧分集解析器 (ep)
 *  匹配: ep12345
 */
class EPParser : public AbstractBaseParser {
public:
    explicit EPParser(int pageSize = 20) : AbstractBaseParser(pageSize) {}

    bool matches(const QString &input) override;
    QString validStr(const QString &input) override;
    model::VideoInfo result(const QString &input, int videoFormat, bool queryLink) override;

private:
    QString m_epId;
    model::VideoInfo getEPDetail(const QString &epId, int videoFormat, bool queryLink);
};

/**
 * @brief 收藏夹/medialist 解析器 (ml)
 *  匹配: ml123456 / https://www.bilibili.com/list/ml123456
 *  收藏夹可视为合集, 支持合并下载
 */
class MLParser : public AbstractBaseParser {
public:
    explicit MLParser(int pageSize = 20) : AbstractBaseParser(pageSize) {}

    bool matches(const QString &input) override;
    QString validStr(const QString &input) override;
    model::VideoInfo result(const QString &input, int videoFormat, bool queryLink) override;

private:
    QString m_mlId;
    model::VideoInfo getMLDetail(const QString &mlId, int videoFormat, bool queryLink);
};

/**
 * @brief UP主空间视频解析器
 *  匹配: https://space.bilibili.com/12345/upload/video
 *        https://space.bilibili.com/12345/channel/seriesdetail?sid=xxx (合集)
 */
class UPSpaceParser : public AbstractBaseParser {
public:
    explicit UPSpaceParser(int pageSize = 30) : AbstractBaseParser(pageSize) {}

    bool matches(const QString &input) override;
    QString validStr(const QString &input) override;
    model::VideoInfo result(const QString &input, int videoFormat, bool queryLink) override;

private:
    QString m_mid;
    QString m_sid;      // 合集 series id (可选)
    QString m_smid;     // 合集所属 mid (可选)
    model::VideoInfo getSpaceVideos(const QString &mid, int videoFormat, bool queryLink);
    model::VideoInfo getSeriesDetail(const QString &mid, const QString &sid,
                                     int videoFormat, bool queryLink);
};

/**
 * @brief 音频解析器 (au)
 *  匹配: au123456
 */
class AudioAuParser : public AbstractBaseParser {
public:
    explicit AudioAuParser(int pageSize = 20) : AbstractBaseParser(pageSize) {}

    bool matches(const QString &input) override;
    QString validStr(const QString &input) override;
    model::VideoInfo result(const QString &input, int videoFormat, bool queryLink) override;

private:
    QString m_auId;
    model::VideoInfo getAuDetail(const QString &auId, int qn, bool queryLink);
};

} // namespace bili::parser

#endif // BILI_CODE_PARSER_IMPL_PARSERS_H
