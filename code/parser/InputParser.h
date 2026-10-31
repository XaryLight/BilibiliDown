#ifndef BILI_CODE_PARSER_INPUT_PARSER_H
#define BILI_CODE_PARSER_INPUT_PARSER_H

#include "impl/Parsers.h"
#include "model/Types.h"

#include <QString>
#include <memory>

namespace bili::parser {

/**
 * @brief 解析分发器 (对应 Java 版 InputParser)
 *
 *  按优先级依次尝试各解析器, 第一个匹配的生效:
 *    1. UPSpaceParser  (space.bilibili.com, weight 69~78)
 *    2. MLParser       (收藏夹 ml, weight 66)
 *    3. SSParser       (番剧 ss/md)
 *    4. EPParser       (番剧分集 ep)
 *    5. AudioAuParser  (音频 au, weight 22)
 *    6. BvParser       (BV/AV/b23 短链, 兜底)
 *
 *  支持 "p=N" 后缀参数 (收藏夹/空间列表页码)
 */
class InputParser {
public:
    InputParser();

    // 解析输入; 失败抛 BiliException
    // videoFormat: 0 MP4(DASH) / 1 FLV / 2 仅音频
    // getVideoLink: 是否同时查询下载链接
    model::VideoInfo parse(const QString &input, int videoFormat = 0, bool getVideoLink = true);

    // 上一次解析使用的解析器名 (调试/显示)
    QString lastParserName() const { return m_lastParserName; }

    // 输入是否可解析 (不发起网络请求)
    bool canParse(const QString &input) const;

private:
    std::unique_ptr<AbstractBaseParser> selectParser(const QString &input);

    int m_page = 1;
    QString m_lastParserName;
};

} // namespace bili::parser

#endif // BILI_CODE_PARSER_INPUT_PARSER_H
