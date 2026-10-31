#include "InputParser.h"
#include <api/Logger.h>

#include <QRegularExpression>
#include <array>
#include <functional>

namespace bili::parser {

using bili::api::Logger;

InputParser::InputParser() = default;

std::unique_ptr<AbstractBaseParser> InputParser::selectParser(const QString &input) {
    // 依次尝试 (顺序即优先级)
    static const std::array<std::pair<QString, std::function<std::unique_ptr<AbstractBaseParser>()>>, 6> factories = {{
        {"UPSpaceParser", [] { return std::make_unique<UPSpaceParser>(); }},
        {"MLParser",      [] { return std::make_unique<MLParser>(); }},
        {"SSParser",      [] { return std::make_unique<SSParser>(); }},
        {"EPParser",      [] { return std::make_unique<EPParser>(); }},
        {"AudioAuParser", [] { return std::make_unique<AudioAuParser>(); }},
        {"BvParser",      [] { return std::make_unique<BvParser>(); }},
    }};

    for (const auto &[name, factory] : factories) {
        auto parser = factory();
        if (parser->matches(input)) {
            m_lastParserName = name;
            Logger::info("匹配解析器: " + name);
            return parser;
        }
    }
    return nullptr;
}

bool InputParser::canParse(const QString &input) const {
    QString cleaned = input;
    static const QRegularExpression pRe(R"RE(^(.*)p=([0-9]+)$)RE");
    auto m = pRe.match(input);
    if (m.hasMatch())
        cleaned = m.captured(1);
    cleaned = cleaned.trimmed();
    if (cleaned.isEmpty())
        return false;
    // 依次试探 (不保留实例)
    static const auto tryAll = [](const QString &in) {
        return UPSpaceParser().matches(in) || MLParser().matches(in) ||
               SSParser().matches(in) || EPParser().matches(in) ||
               AudioAuParser().matches(in) || BvParser().matches(in);
    };
    return tryAll(cleaned);
}

model::VideoInfo InputParser::parse(const QString &input, int videoFormat, bool getVideoLink) {
    QString cleaned = input.trimmed();
    if (cleaned.isEmpty())
        throw BiliException("输入为空");

    // 提取 p=N 页码参数
    static const QRegularExpression pRe(R"RE(^(.*)p=([0-9]+)$)RE");
    auto m = pRe.match(cleaned);
    if (m.hasMatch()) {
        m_page = m.captured(2).toInt();
        cleaned = m.captured(1).trimmed();
    }

    auto parser = selectParser(cleaned);
    if (!parser)
        throw BiliException("无法解析输入, 请检查链接格式: " + input);

    model::VideoInfo vi = parser->result(cleaned, videoFormat, getVideoLink);
    vi.videoLink = input;
    return vi;
}

} // namespace bili::parser
