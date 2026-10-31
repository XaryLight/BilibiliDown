#include "Parsers.h"
#include <api/Logger.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <QtConcurrent/QtConcurrent>
#include <QSemaphore>
#include <QFuture>
#include <QList>
#include <QThread>

namespace bili::parser {

using bili::api::Logger;

// ============================================================
//  BvParser: BV/AV/b23短链
// ============================================================
QString BvParser::resolveB23(const QString &shortUrl) {
    // b23.tv 短链需要跟随 302 重定向拿到真实地址
    auto r = bili::api::HttpClient::instance().getSync(shortUrl);
    if (r.ok && !r.finalUrl.isEmpty()) {
        QString final = r.finalUrl.toString();
        Logger::info("b23 短链解析: " + shortUrl + " -> " + final);
        return final;
    }
    return shortUrl;
}

model::VideoInfo BvParser::result(const QString &input, int videoFormat, bool queryLink) {
    QString real = input;
    if (input.contains("b23.tv"))
        real = resolveB23(input);

    // 提取 BV 号
    static const QRegularExpression bvRe(R"RE(BV1[a-zA-Z0-9]{9})RE");
    auto m = bvRe.match(real);
    QString bvId;
    if (m.hasMatch()) {
        bvId = m.captured(0);
    } else {
        // av 号
        static const QRegularExpression avRe(R"RE(av(\d{3,}))RE");
        auto m2 = avRe.match(real);
        if (m2.hasMatch()) {
            int64_t avNum = m2.captured(1).toLongLong();
            bvId = bili::api::Crypto::av2bv(avNum);
        }
    }
    if (bvId.isEmpty())
        throw BiliException("无法从输入中解析出 BV/AV 号: " + input);

    model::VideoInfo vi = getAVDetail(bvId, videoFormat, queryLink);
    vi.videoLink = input;
    vi.isCollection = vi.clips.size() > 1;
    return vi;
}

// ============================================================
//  SSParser: 番剧 (ss/md)
// ============================================================
bool SSParser::matches(const QString &input) {
    // 仅匹配: 纯 ss/md 输入, 或 bangumi 播放页/媒体页 url (避免 BV 号内 ss/ml 字样误匹配)
    static const QRegularExpression re(R"RE(^(?:ss|md)(\d+)$)RE");
    static const QRegularExpression urlRe(R"RE(bangumi/(?:play/ss|media/md)(\d+))RE");
    auto m = re.match(input);
    if (m.hasMatch()) {
        m_ssId = (input.startsWith("md") ? "md" : "ss") + m.captured(1);
        return true;
    }
    auto m2 = urlRe.match(input);
    if (m2.hasMatch()) {
        bool isMd = input.contains("/media/md");
        m_ssId = (isMd ? "md" : "ss") + m2.captured(1);
        return true;
    }
    return false;
}

QString SSParser::validStr(const QString &input) {
    Q_UNUSED(input)
    return m_ssId;
}

model::VideoInfo SSParser::result(const QString &input, int videoFormat, bool queryLink) {
    Q_UNUSED(input)
    Logger::info("SSParser 获取结果: " + m_ssId);
    if (m_ssId.startsWith("md"))
        return getSSDetail(mdIdToSsId(m_ssId.mid(2)), videoFormat, queryLink);
    return getSSDetail(m_ssId, videoFormat, queryLink);
}

QString SSParser::mdIdToSsId(const QString &mdId) {
    QString url = "https://api.bilibili.com/pgc/review/user?media_id=" + mdId;
    Logger::info("查询 md->ss: " + url);
    QString json = httpGet(url, bili::api::HttpClient::defaultHeaders("https://www.bilibili.com/"));
    QJsonObject media = QJsonDocument::fromJson(json.toUtf8()).object()
                            .value("result").toObject().value("media").toObject();
    int ssId = media.value("season_id").toInt();
    if (ssId == 0)
        throw BiliException("md -> ss 转换失败: md" + mdId);
    return "ss" + QString::number(ssId);
}

model::VideoInfo SSParser::getSSDetail(const QString &ssId, int videoFormat, bool queryLink) {
    model::VideoInfo vi;
    vi.videoId = ssId;

    QString ssIdNumber = ssId.mid(2);
    QString url = "https://api.bilibili.com/pgc/view/web/season?season_id=" + ssIdNumber;
    Logger::info("查询番剧信息: " + url);
    QString json = httpGet(url, bili::api::HttpClient::defaultHeaders("https://www.bilibili.com/"));
    QJsonObject jObj = QJsonDocument::fromJson(json.toUtf8()).object().value("result").toObject();
    if (jObj.isEmpty())
        throw BiliException("番剧信息查询失败: " + ssId);

    vi.videoName = jObj.value("title").toString();
    vi.brief = jObj.value("evaluate").toString();
    vi.author = "番剧";
    vi.authorId = "番剧";
    vi.videoPreview = jObj.value("cover").toString();
    vi.isCollection = true;

    QJsonArray array = jObj.value("episodes").toArray();
    QList<int> qnListDefault;
    if (array.size() > 20)
        qnListDefault = QList<int>{120, 116, 112, 80, 74, 64, 32, 16};

    QString lastAvId;
    int lastPage = 0;
    for (int i = 0; i < array.size(); i++) {
        QJsonObject clipObj = array.at(i).toObject();
        model::ClipInfo clip;
        clip.avTitle = vi.videoName;
        QString bvid = clipObj.value("bvid").toString();
        clip.avId = bvid;
        clip.cid = clipObj.value("cid").toVariant().toLongLong();
        clip.remark = i + 1;
        clip.picPreview = clipObj.value("cover").toString();
        if (i > 0 && lastAvId == bvid) {
            lastPage++;
            clip.page = lastPage;
        } else {
            lastPage = 1;
            clip.page = 1;
            lastAvId = bvid;
        }
        clip.title = clipObj.value("long_title").toString();
        clip.upName = vi.videoName;
        clip.upId = ssId;

        try {
            QList<int> qnList = qnListDefault.isEmpty() ? getVideoQNList(bvid, QString::number(clip.cid)) : qnListDefault;
            for (int qn : qnList) {
                if (queryLink)
                    clip.links.insert(qn, getVideoLink(bvid, QString::number(clip.cid), qn, videoFormat));
                else
                    clip.links.insert(qn, QString());
            }
        } catch (const std::exception &e) {
            Logger::warn(QString("番剧 clip %1 链接查询失败: %2").arg(i).arg(e.what()));
        }
        vi.clips.append(clip);
    }
    return vi;
}

// ============================================================
//  EPParser: 番剧分集
// ============================================================
bool EPParser::matches(const QString &input) {
    static const QRegularExpression re(R"RE(^ep(\d+)$)RE");
    static const QRegularExpression urlRe(R"RE(bangumi/play/ep(\d+))RE");
    auto m = re.match(input);
    if (m.hasMatch()) {
        m_epId = "ep" + m.captured(1);
        return true;
    }
    auto m2 = urlRe.match(input);
    if (m2.hasMatch()) {
        m_epId = "ep" + m2.captured(1);
        return true;
    }
    return false;
}

QString EPParser::validStr(const QString &input) {
    Q_UNUSED(input)
    return m_epId;
}

model::VideoInfo EPParser::result(const QString &input, int videoFormat, bool queryLink) {
    Q_UNUSED(input)
    return getEPDetail(m_epId, videoFormat, queryLink);
}

model::VideoInfo EPParser::getEPDetail(const QString &epId, int videoFormat, bool queryLink) {
    // ep -> 先查 season 信息找到对应 clip
    QString epNum = epId.mid(2);
    QString url = "https://api.bilibili.com/pgc/view/web/season?ep_id=" + epNum;
    Logger::info("查询番剧ep: " + url);
    QString json = httpGet(url, bili::api::HttpClient::defaultHeaders("https://www.bilibili.com/"));
    QJsonObject jObj = QJsonDocument::fromJson(json.toUtf8()).object().value("result").toObject();
    if (jObj.isEmpty())
        throw BiliException("番剧信息查询失败: " + epId);

    model::VideoInfo vi;
    vi.videoId = epId;
    vi.videoName = jObj.value("title").toString();
    vi.brief = jObj.value("evaluate").toString();
    vi.author = "番剧";
    vi.authorId = "番剧";
    vi.videoPreview = jObj.value("cover").toString();

    QJsonArray array = jObj.value("episodes").toArray();
    for (int i = 0; i < array.size(); i++) {
        QJsonObject clipObj = array.at(i).toObject();
        int64_t cid = clipObj.value("cid").toVariant().toLongLong();
        if (QString::number(cid) != epNum && clipObj.value("id").toInt() != epNum.toInt())
            continue;

        model::ClipInfo clip;
        clip.avTitle = vi.videoName;
        clip.avId = clipObj.value("bvid").toString();
        clip.cid = cid;
        clip.remark = i + 1;
        clip.page = 1;
        clip.picPreview = clipObj.value("cover").toString();
        clip.title = clipObj.value("long_title").toString();
        clip.upName = vi.videoName;
        clip.upId = epId;

        try {
            QList<int> qnList = getVideoQNList(clip.avId, QString::number(cid));
            for (int qn : qnList) {
                if (queryLink)
                    clip.links.insert(qn, getVideoLink(clip.avId, QString::number(cid), qn, videoFormat));
                else
                    clip.links.insert(qn, QString());
            }
        } catch (const std::exception &e) {
            Logger::warn(QString("ep 链接查询失败: %1").arg(e.what()));
        }
        vi.clips.append(clip);
        break;
    }
    if (vi.clips.isEmpty())
        throw BiliException("未找到对应分集: " + epId);
    return vi;
}

// ============================================================
//  MLParser: 收藏夹 (medialist)
// ============================================================
bool MLParser::matches(const QString &input) {
    static const QRegularExpression re(R"RE(^ml(\d+)$)RE");
    static const QRegularExpression urlRe(R"RE(/(?:list/)?ml(\d+))RE");
    auto m = re.match(input);
    if (m.hasMatch()) {
        m_mlId = m.captured(1);
        return true;
    }
    auto m2 = urlRe.match(input);
    if (m2.hasMatch()) {
        m_mlId = m2.captured(1);
        return true;
    }
    return false;
}

QString MLParser::validStr(const QString &input) {
    Q_UNUSED(input)
    return "ml" + m_mlId + "p=" + QString::number(1);
}

model::VideoInfo MLParser::result(const QString &input, int videoFormat, bool queryLink) {
    Q_UNUSED(input)
    return getMLDetail(m_mlId, videoFormat, queryLink);
}

model::VideoInfo MLParser::getMLDetail(const QString &mlId, int videoFormat, bool queryLink) {
    model::VideoInfo vi;
    vi.videoId = "ml" + mlId;
    vi.isCollection = true;

    QString url = QString("https://api.bilibili.com/medialist/gateway/base/detail?media_id=%1&pn=1&ps=%2")
                      .arg(mlId).arg(m_pageSize);
    Logger::info("查询收藏夹: " + url);
    QString json = httpGet(url, bili::api::HttpClient::defaultHeaders());
    QJsonObject jobj = QJsonDocument::fromJson(json.toUtf8()).object();
    QJsonObject jData = jobj.value("data").toObject();
    QJsonObject jInfo = jData.value("info").toObject();
    if (jInfo.isEmpty())
        throw BiliException("收藏夹查询失败: ml" + mlId);

    vi.videoName = jInfo.value("title").toString();
    vi.brief = jInfo.value("upper").toObject().value("name").toString() + " 的收藏夹";
    vi.author = jInfo.value("upper").toObject().value("name").toString();
    vi.authorId = QString::number(jInfo.value("upper").toObject().value("mid").toVariant().toLongLong());
    vi.videoPreview = jInfo.value("cover").toString();

    QJsonArray arr = jData.value("medias").toArray();
    for (int i = 0; i < arr.size(); i++) {
        QJsonObject jAV = arr.at(i).toObject();
        QString avId = jAV.value("bvid").toString();
        QString avTitle = jAV.value("title").toString();
        QString upName = jAV.value("upper").toObject().value("name").toString();
        QString upId = QString::number(jAV.value("upper").toObject().value("mid").toVariant().toLongLong());
        int64_t favTime = jAV.value("fav_time").toVariant().toLongLong() * 1000;
        int64_t cTime = jAV.value("ctime").toVariant().toLongLong() * 1000;
        QJsonArray jClips = jAV.value("pages").toArray();

        if (jClips.isEmpty()) {
            QString link = jAV.value("link").toString();
            if (link.startsWith("bilibili://music")) {
                // 收藏夹中的音频
                model::ClipInfo clip;
                clip.avTitle = avTitle;
                clip.avId = "au" + QString::number(jAV.value("id").toVariant().toLongLong());
                clip.cid = jAV.value("id").toVariant().toLongLong();
                clip.page = 1;
                clip.title = avTitle;
                clip.picPreview = jAV.value("cover").toString();
                clip.remark = i + 1;
                clip.listName = jInfo.value("title").toString();
                clip.listOwnerName = vi.author;
                clip.listOwnerId = vi.authorId;
                clip.upName = upName;
                clip.upId = upId;
                clip.favTime = favTime;
                clip.cTime = cTime;
                clip.links.insert(3, QString());
                clip.links.insert(2, QString());
                clip.links.insert(1, QString());
                clip.links.insert(0, QString());
                vi.clips.append(clip);
            }
            continue;
        }

        for (int p = 0; p < jClips.size(); p++) {
            QJsonObject jClip = jClips.at(p).toObject();
            model::ClipInfo clip;
            clip.avId = avId;
            clip.cid = jClip.value("id").toVariant().toLongLong();
            clip.page = jClip.value("page").toInt();
            clip.remark = i + 1;
            clip.picPreview = jAV.value("cover").toString();
            clip.listName = jInfo.value("title").toString();
            clip.listOwnerName = vi.author;
            clip.listOwnerId = vi.authorId;
            clip.upName = upName;
            clip.upId = upId;
            clip.avTitle = avTitle;
            clip.title = jClip.value("title").toString();
            clip.favTime = favTime;
            clip.cTime = cTime;

            try {
                for (int qn : QList<int>{120, 116, 112, 80, 74, 64, 32, 16}) {
                    if (queryLink)
                        clip.links.insert(qn, getVideoLink(avId, QString::number(clip.cid), qn, videoFormat));
                    else
                        clip.links.insert(qn, QString());
                }
            } catch (const std::exception &e) {
                Logger::warn(QString("收藏夹 clip 链接查询失败: %1").arg(e.what()));
            }
            vi.clips.append(clip);
        }
    }
    return vi;
}

// ============================================================
//  UPSpaceParser: UP主空间视频 / 合集
// ============================================================
bool UPSpaceParser::matches(const QString &input) {
    static const QRegularExpression re(R"RE(space\.bilibili\.com/(\d+))RE");
    auto m = re.match(input);
    if (!m.hasMatch())
        return false;
    m_mid = m.captured(1);
    // 合集: channel/seriesdetail?sid=xxx
    static const QRegularExpression sidRe(R"RE(sid=(\d+))RE");
    auto m2 = sidRe.match(input);
    if (m2.hasMatch()) {
        m_sid = m2.captured(1);
        return true;
    }
    return true;
}

QString UPSpaceParser::validStr(const QString &input) {
    Q_UNUSED(input)
    return m_sid.isEmpty() ? QString("space:%1").arg(m_mid)
                           : QString("space:%1:series:%2").arg(m_mid, m_sid);
}

model::VideoInfo UPSpaceParser::result(const QString &input, int videoFormat, bool queryLink) {
    Q_UNUSED(input)
    if (!m_sid.isEmpty())
        return getSeriesDetail(m_mid, m_sid, videoFormat, queryLink);
    return getSpaceVideos(m_mid, videoFormat, queryLink);
}

model::VideoInfo UPSpaceParser::getSpaceVideos(const QString &mid, int videoFormat, bool queryLink) {
    model::VideoInfo vi;
    vi.videoId = "space:" + mid;
    vi.isCollection = true;

    // 空间页接口统一带上空间页 Referer (与浏览器一致, 降低风控概率)
    const QString spaceRef = QString("https://space.bilibili.com/%1/upload/video").arg(mid);

    // UP 主基本信息
    QString infoUrl = QString("https://api.bilibili.com/x/space/wbi/acc/info?mid=%1").arg(mid);
    QString infoJson = httpGet(bili::api::Crypto::wbiSignAuto(infoUrl + bili::api::Crypto::genDmImgParams()),
                               bili::api::HttpClient::jsonApiHeaders(spaceRef));
    QJsonObject info = QJsonDocument::fromJson(infoJson.toUtf8()).object().value("data").toObject();
    vi.author = info.value("name").toString();
    vi.authorId = mid;
    vi.videoName = vi.author + " 的投稿";
    vi.videoPreview = info.value("face").toString();

    // 投稿列表 (wbi arc/search)
    int page = 1;
    bool hasMore = true;
    int total = 0;
    while (hasMore && total < 500) {
        QString url = QString("https://api.bilibili.com/x/space/wbi/arc/search?mid=%1&ps=30&tid=0&pn=%2"
                              "&keyword=&order=pubdate&order_avoided=true&platform=web&web_location=1315873")
                          .arg(mid).arg(page);
        url = bili::api::Crypto::wbiSignAuto(url + bili::api::Crypto::genDmImgParams());
        QString json;
        try {
            json = httpGet(url, bili::api::HttpClient::jsonApiHeaders(spaceRef));
        } catch (const BiliException &e) {
            // 风控重试后仍失败: 有部分结果则返回部分, 否则抛出
            if (vi.clips.isEmpty())
                throw;
            Logger::warn(QString("UP主空间分页 p%1 失败(%2), 返回已获取的 %3 个视频")
                             .arg(page).arg(e.message()).arg(total));
            vi.brief += QString("\n(分页在第%1页被B站风控限制, 已获取 %2 个视频, 完整列表请稍后重试)")
                            .arg(page).arg(total);
            break;
        }
        QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
        if (obj.value("code").toInt() != 0) {
            // 风控/错误码 (httpGet 内已重试): 空结果会被误判为列表结束, 必须显式处理
            if (vi.clips.isEmpty())
                throw BiliException(QString("UP主视频列表查询失败(code=%1): %2")
                                        .arg(obj.value("code").toInt())
                                        .arg(obj.value("message").toString()));
            Logger::warn(QString("UP主空间分页 p%1 返回错误(code=%2), 返回已获取的 %3 个视频")
                             .arg(page).arg(obj.value("code").toInt()).arg(total));
            vi.brief += QString("\n(分页在第%1页被B站风控限制, 已获取 %2 个视频, 完整列表请稍后重试)")
                            .arg(page).arg(total);
            break;
        }
        QJsonObject data = obj.value("data").toObject();
        QJsonObject pageObj = data.value("page").toObject();
        QJsonArray vlist = data.value("list").toArray(); // 旧接口; 新接口在 list.vlist
        if (vlist.isEmpty())
            vlist = data.value("list").toObject().value("vlist").toArray();
        if (vlist.isEmpty())
            break;

        for (const QJsonValue &v : vlist) {
            QJsonObject jv = v.toObject();
            model::ClipInfo clip;
            clip.avId = jv.value("bvid").toString();
            clip.avTitle = jv.value("title").toString();
            // 注意: wbi arc/search 的 vlist 不含 cid, 置 0 由下载时懒加载补查
            // (TaskRunner::processClip -> resolveCid); 多P视频此处呈现为单条(第1P)
            clip.cid = jv.value("cid").toVariant().toLongLong();
            clip.page = 1;
            clip.remark = total + 1;
            clip.title = jv.value("title").toString();
            clip.picPreview = jv.value("pic").toString();
            clip.upName = vi.author;
            clip.upId = mid;
            clip.cTime = jv.value("created").toVariant().toLongLong();
            if (!clip.avId.isEmpty())
                vi.clips.append(clip);
            total++;
        }
        int count = pageObj.value("count").toInt();
        int ps = pageObj.value("ps").toInt(30);
        hasMore = (page * ps) < count;
        page++;
        if (page > 17) break; // 防御: 最多500条
        QThread::msleep(300); // 分页节流, 避免连续请求触发风控
    }
    if (vi.clips.isEmpty())
        throw BiliException("未获取到UP主视频: " + mid);
    return vi;
}

model::VideoInfo UPSpaceParser::getSeriesDetail(const QString &mid, const QString &sid,
                                                int videoFormat, bool queryLink) {
    model::VideoInfo vi;
    vi.videoId = QString("space:%1:series:%2").arg(mid, sid);
    vi.isCollection = true;

    // ---- 1. 合集元数据 + 稿件列表 (分页拉取) ----
    // 注意:
    //  - page_num 从 1 开始, 传 0 会被服务端拒绝 (code=-400 请求错误)
    //  - mid 任意非负整数即可, 无需真实 UP 主 mid
    //  - 该接口 archives 不含 cid, 见第 3 步补全
    struct ArchiveItem {
        QString bvid;
        QString title;
        QString pic;
        int64_t ctime = 0;
    };
    QList<ArchiveItem> items;
    QString metaName, metaCover, metaDesc;
    qint64 metaMid = mid.toLongLong();
    const QString spaceRef =
        QString("https://space.bilibili.com/%1/channel/collectiondetail?sid=%2").arg(mid, sid);

    int pageNum = 1;
    const int pageSize = 30;
    bool truncated = false; // 分页被风控截断
    while (pageNum <= 34) { // 防御性上限: 最多约 1000 条
        QString url = QString("https://api.bilibili.com/x/polymer/web-space/seasons_archives_list"
                              "?mid=%1&season_id=%2&sort_reverse=false&page_num=%3&page_size=%4")
                          .arg(mid).arg(sid).arg(pageNum).arg(pageSize);
        Logger::info("查询UP主合集: " + url);
        QString json;
        try {
            json = httpGet(url, bili::api::HttpClient::jsonApiHeaders(spaceRef));
        } catch (const BiliException &e) {
            if (items.isEmpty())
                throw;
            Logger::warn(QString("合集分页 p%1 失败(%2), 返回已获取的 %3 个视频")
                             .arg(pageNum).arg(e.message()).arg(items.size()));
            truncated = true;
            break;
        }
        QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
        if (obj.value("code").toInt() != 0) {
            if (items.isEmpty())
                throw BiliException(QString("合集查询失败(code=%1): %2")
                                        .arg(obj.value("code").toInt())
                                        .arg(obj.value("message").toString()));
            Logger::warn(QString("合集分页 p%1 返回错误(code=%2), 返回已获取的 %3 个视频")
                             .arg(pageNum).arg(obj.value("code").toInt()).arg(items.size()));
            truncated = true;
            break;
        }
        QJsonObject data = obj.value("data").toObject();
        if (pageNum == 1) {
            QJsonObject meta = data.value("meta").toObject();
            metaName = meta.value("name").toString();
            metaCover = meta.value("cover").toString();
            metaDesc = meta.value("description").toString();
            metaMid = meta.value("mid").toVariant().toLongLong();
            if (metaMid <= 0)
                metaMid = mid.toLongLong();
        }
        QJsonArray archives = data.value("archives").toArray();
        for (const QJsonValue &v : archives) {
            QJsonObject jv = v.toObject();
            ArchiveItem it;
            it.bvid = jv.value("bvid").toString();
            it.title = jv.value("title").toString();
            it.pic = jv.value("pic").toString();
            it.ctime = jv.value("ctime").toVariant().toLongLong();
            if (!it.bvid.isEmpty())
                items.append(it);
        }
        int total = data.value("page").toObject().value("total").toInt(items.size());
        if (archives.isEmpty() || items.size() >= total)
            break;
        pageNum++;
        QThread::msleep(300); // 分页节流, 避免连续请求触发风控
    }
    if (items.isEmpty())
        throw BiliException("未获取到合集视频: sid=" + sid);

    vi.videoName = metaName.isEmpty() ? QString("合集%1").arg(sid) : metaName;
    vi.videoPreview = metaCover;
    vi.brief = metaDesc;
    if (truncated)
        vi.brief += QString("\n(分页在第%1页被B站风控限制, 已获取 %2 个视频, 完整合集请稍后重试)")
                        .arg(pageNum).arg(items.size());
    vi.authorId = QString::number(metaMid);
    vi.author = "UP主" + vi.authorId; // meta 不含昵称, 先用兜底值

    // ---- 2. UP 主昵称 (acc/info, 失败不影响主流程) ----
    try {
        QString infoUrl = QString("https://api.bilibili.com/x/space/wbi/acc/info?mid=%1").arg(vi.authorId);
        QString infoJson = httpGet(bili::api::Crypto::wbiSignAuto(infoUrl + bili::api::Crypto::genDmImgParams()),
                                   bili::api::HttpClient::jsonApiHeaders(spaceRef));
        QString name = QJsonDocument::fromJson(infoJson.toUtf8()).object()
                           .value("data").toObject().value("name").toString();
        if (!name.isEmpty())
            vi.author = name;
    } catch (const std::exception &e) {
        Logger::warn(QString("获取UP主昵称失败, 使用兜底值: %1").arg(e.what()));
    }

    // ---- 3. 逐稿件补全 cid (接口不返回 cid; 合集稿件通常单P, 多P时展开为多个 clip) ----
    //     并发查询控制耗时, 单个失败仅跳过并告警
    auto resolveOne = [&](int i) -> QList<model::ClipInfo> {
        QList<model::ClipInfo> clips;
        const ArchiveItem &it = items.at(i);
        try {
            QString plUrl = QString("https://api.bilibili.com/x/player/pagelist?bvid=%1&jsonp=jsonp").arg(it.bvid);
            QString plJson = httpGet(plUrl, bili::api::HttpClient::jsonApiHeaders(
                                              QString("https://www.bilibili.com/video/%1").arg(it.bvid)));
            QJsonArray pages = QJsonDocument::fromJson(plJson.toUtf8()).object().value("data").toArray();
            for (const QJsonValue &pv : pages) {
                QJsonObject po = pv.toObject();
                int64_t cid = po.value("cid").toVariant().toLongLong();
                if (cid <= 0)
                    continue;
                model::ClipInfo clip;
                clip.avId = it.bvid;
                clip.avTitle = it.title;
                clip.cid = cid;
                clip.page = po.value("page").toInt();
                clip.title = po.value("part").toString();
                clip.picPreview = it.pic;
                clip.cTime = it.ctime;
                clip.upName = vi.author;
                clip.upId = vi.authorId;
                clip.listName = metaName;
                clips.append(clip);
            }
        } catch (const std::exception &e) {
            Logger::warn(QString("合集 sid=%1 第%2项(%3) 分P查询失败: %4")
                             .arg(sid).arg(i + 1).arg(it.bvid, e.what()));
        }
        return clips;
    };

    const int kConcurrency = 4; // 并发上限, 避免触发风控
    QSemaphore sem(kConcurrency);
    auto worker = [&](int i) -> QList<model::ClipInfo> {
        QList<model::ClipInfo> clips = resolveOne(i);
        sem.release();
        return clips;
    };
    QList<QFuture<QList<model::ClipInfo>>> futs;
    futs.reserve(items.size());
    for (int i = 0; i < items.size(); ++i) {
        sem.acquire();
        futs << QtConcurrent::run(worker, i);
    }

    int idx = 0;
    for (int i = 0; i < futs.size(); ++i) {
        for (model::ClipInfo clip : futs.at(i).result()) {
            clip.remark = ++idx;
            vi.clips.append(clip);
        }
    }
    if (vi.clips.isEmpty())
        throw BiliException("未获取到合集视频: sid=" + sid);
    Q_UNUSED(videoFormat); Q_UNUSED(queryLink);
    return vi;
}

// ============================================================
//  AudioAuParser: 音频
// ============================================================
bool AudioAuParser::matches(const QString &input) {
    static const QRegularExpression re(R"RE(^au(\d+)$)RE");
    static const QRegularExpression urlRe(R"RE(/audio/au(\d+))RE");
    auto m = re.match(input);
    if (m.hasMatch()) {
        m_auId = "au" + m.captured(1);
        return true;
    }
    auto m2 = urlRe.match(input);
    if (m2.hasMatch()) {
        m_auId = "au" + m2.captured(1);
        return true;
    }
    return false;
}

QString AudioAuParser::validStr(const QString &input) {
    Q_UNUSED(input)
    return m_auId;
}

model::VideoInfo AudioAuParser::result(const QString &input, int videoFormat, bool queryLink) {
    Q_UNUSED(input); Q_UNUSED(videoFormat); Q_UNUSED(queryLink);
    return getAuDetail(m_auId, 3, queryLink);
}

model::VideoInfo AudioAuParser::getAuDetail(const QString &auId, int qn, bool queryLink) {
    model::VideoInfo vi;
    vi.videoId = auId;
    QString auNum = auId.mid(2);

    QString url = QString("https://www.bilibili.com/audio/music-service-c/web/song/info?sid=%1").arg(auNum);
    QString json = httpGet(url, bili::api::HttpClient::defaultHeaders());
    QJsonObject data = QJsonDocument::fromJson(json.toUtf8()).object().value("data").toObject();
    vi.videoName = data.value("title").toString();
    vi.author = data.value("author").toString();
    vi.videoPreview = data.value("cover").toString();
    vi.brief = data.value("intro").toString();

    model::ClipInfo clip;
    clip.avId = auId;
    clip.avTitle = vi.videoName;
    clip.cid = auNum.toLongLong();
    clip.page = 1;
    clip.title = vi.videoName;
    clip.upName = vi.author;
    if (queryLink) {
        clip.links.insert(qn, getAudioLink(auId, qn));
    } else {
        clip.links.insert(qn, QString());
    }
    vi.clips.append(clip);
    return vi;
}

} // namespace bili::parser
