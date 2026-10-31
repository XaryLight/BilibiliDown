#include "AbstractBaseParser.h"

#include <api/HttpClient.h>
#include <api/Crypto.h>
#include <api/Logger.h>
#include <api/Config.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QThread>

namespace bili::parser {

using bili::api::HttpClient;
using bili::api::Crypto;
using bili::api::Config;
using bili::api::Logger;

AbstractBaseParser::AbstractBaseParser(int pageSize) : m_pageSize(pageSize) {}

QString AbstractBaseParser::httpGet(const QString &url, const api::Headers &headers) {
    // B站风控应对: HTTP 412 (WAF 限流) / 429 / 503 及业务码 -352 (风控校验失败)
    // 均为短时限流, 退避重试而非直接失败; 指纹 Cookie 已由 HttpClient 自动携带
    const int kMaxAttempts = 4;
    for (int attempt = 1; ; ++attempt) {
        auto r = HttpClient::instance().getSync(url, headers);
        int bizCode = r.ok ? QJsonDocument::fromJson(r.body).object().value("code").toInt() : 0;
        bool riskCtrl = r.ok && (r.statusCode == 412 || bizCode == -352);
        bool netRetryable = !r.ok && (r.statusCode == 412 || r.statusCode == 429 ||
                                      r.statusCode == 503);
        if ((riskCtrl || netRetryable) && attempt < kMaxAttempts) {
            static const int kDelays[] = {2000, 5000, 10000}; // 2s / 5s / 10s
            int delayMs = kDelays[attempt - 1];
            Logger::warn(QString("触发B站风控(HTTP %1/code %2), %3ms 后重试 %4/%5: %6")
                             .arg(r.statusCode).arg(bizCode).arg(delayMs)
                             .arg(attempt).arg(kMaxAttempts - 1).arg(url.left(150)));
            QThread::msleep(delayMs);
            continue;
        }
        if (!r.ok) {
            if (r.statusCode == 412)
                throw BiliException("请求被B站风控拦截(HTTP 412)，请稍后重试、降低解析频率，或登录账号后重试");
            throw BiliException(QString("网络请求失败: %1 (%2)").arg(url, r.error));
        }
        return QString::fromUtf8(r.body);
    }
}

QString AbstractBaseParser::httpGet(const QString &url) {
    return httpGet(url, HttpClient::jsonApiHeaders());
}

QString AbstractBaseParser::wbiUrl(const QString &url) {
    QString u = url + Crypto::genDmImgParams();
    return Crypto::wbiSignAuto(u);
}

// ============================================================
//  AV/BV 详情查询
// ============================================================
model::VideoInfo AbstractBaseParser::getAVDetail(const QString &bvId, int videoFormat, bool queryLink) {
    model::VideoInfo vi;
    vi.videoId = bvId;

    // 1. 分P列表
    QString url = QString("https://api.bilibili.com/x/player/pagelist?bvid=%1&jsonp=jsonp").arg(bvId);
    Logger::info("查询分P列表: " + url);
    QString json = httpGet(url, HttpClient::jsonApiHeaders());
    QJsonObject pagelistObj = QJsonDocument::fromJson(json.toUtf8()).object();
    QJsonArray array = pagelistObj.value("data").toArray();
    if (array.isEmpty())
        throw BiliException("无法获取分P列表 (视频不存在或需要登录): " + bvId);

    // 2. 总体信息 (wbi view/detail)
    int64_t aid = Crypto::bv2av(bvId);
    QString detailUrl = QString(
        "https://api.bilibili.com/x/web-interface/wbi/view/detail?platform=web&page_no=1&p=1"
        "&need_operation_card=1&web_rm_repeat=1&need_elec=1&bvid=%1").arg(bvId);
    detailUrl = wbiUrl(detailUrl);
    QString detailJson = httpGet(detailUrl, HttpClient::jsonApiHeaders());
    QJsonObject detailRaw = QJsonDocument::fromJson(detailJson.toUtf8()).object();

    int videoCnt = 0;
    int64_t ctime = 0;
    if (detailRaw.value("code").toInt() == -403) {
        // 风控/权限场景回退: fav 资源信息接口
        QString favUrl = QString("https://api.bilibili.com/x/v3/fav/resource/infos?resources=%1:2"
                                 "&platform=web&folder_mid=&folder_id=").arg(aid);
        QString favJson = httpGet(favUrl, HttpClient::jsonApiHeaders());
        QJsonObject detailObj = QJsonDocument::fromJson(favJson.toUtf8()).object()
                                    .value("data").toArray().at(0).toObject();
        ctime = detailObj.value("ctime").toVariant().toLongLong() * 1000;
        videoCnt = detailObj.value("page").toInt();
        vi.videoName = detailObj.value("title").toString();
        vi.brief = detailObj.value("intro").toString();
        vi.author = detailObj.value("upper").toObject().value("name").toString();
        vi.authorId = QString::number(detailObj.value("upper").toObject().value("mid").toVariant().toLongLong());
        vi.videoPreview = detailObj.value("cover").toString();
    } else {
        QJsonObject detailObj = detailRaw.value("data").toObject().value("View").toObject();
        ctime = detailObj.value("ctime").toVariant().toLongLong() * 1000;
        videoCnt = detailObj.value("videos").toInt();
        vi.videoName = detailObj.value("title").toString();
        vi.brief = detailObj.value("desc").toString();
        vi.author = detailObj.value("owner").toObject().value("name").toString();
        vi.authorId = QString::number(detailObj.value("owner").toObject().value("mid").toVariant().toLongLong());
        vi.videoPreview = detailObj.value("pic").toString();
    }

    // 3. 互动视频 (videoCnt > 1 但 pagelist 只有1个) -> 简化为普通处理 (不展开故事线)
    //    保留与 Java 版一致的行为: 若 pagelist 有多项, 逐项生成 clip

    QList<int> qnListDefault;
    if (array.size() > 5)
        qnListDefault = QList<int>{120, 116, 112, 80, 74, 64, 32, 16};

    for (int i = 0; i < array.size(); i++) {
        QJsonObject jObj = array.at(i).toObject();
        int64_t cid = jObj.value("cid").toVariant().toLongLong();
        model::ClipInfo clip;
        clip.avTitle = vi.videoName;
        clip.avId = bvId;
        clip.cid = cid;
        clip.page = jObj.value("page").toInt();
        clip.title = jObj.value("part").toString();
        clip.picPreview = vi.videoPreview;
        clip.upName = vi.author;
        clip.upId = vi.authorId;
        clip.cTime = ctime;

        try {
            QList<int> qnList = qnListDefault.isEmpty() ? getVideoQNList(bvId, QString::number(cid)) : qnListDefault;
            for (int qn : qnList) {
                if (queryLink) {
                    QString link = getVideoLink(bvId, QString::number(cid), qn, videoFormat);
                    clip.links.insert(qn, link);
                } else {
                    clip.links.insert(qn, QString());
                }
            }
        } catch (const std::exception &e) {
            Logger::warn(QString("查询 %1 p%2 链接失败: %3").arg(bvId).arg(clip.page).arg(e.what()));
        }
        vi.clips.append(clip);
    }
    return vi;
}

// ============================================================
//  cid 懒加载 (UP主空间列表等解析期无 cid 的场景)
// ============================================================
int64_t AbstractBaseParser::resolveCid(const QString &bvId, int page) {
    QString url = QString("https://api.bilibili.com/x/player/pagelist?bvid=%1&jsonp=jsonp").arg(bvId);
    Logger::info("查询分P列表(cid): " + url);
    QString json = httpGet(url, HttpClient::jsonApiHeaders());
    QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).object().value("data").toArray();
    if (array.isEmpty())
        throw BiliException("无法获取分P列表 (cid 解析失败): " + bvId);
    int idx = page > 0 ? qMin(page, array.size()) - 1 : 0;
    int64_t cid = array.at(idx).toObject().value("cid").toVariant().toLongLong();
    if (cid <= 0)
        throw BiliException("分P列表中无有效 cid: " + bvId);
    return cid;
}

// ============================================================
//  清晰度列表查询
// ============================================================
QList<int> AbstractBaseParser::getVideoQNList(const QString &bvId, const QString &cid) {
    QString url = QString("https://api.bilibili.com/x/player/playurl?cid=%1&bvid=%2&qn=32"
                          "&type=&otype=json&fnver=0&fnval=4048&fourk=1").arg(cid, bvId);
    QString json = httpGet(url, HttpClient::jsonApiHeaders());
    QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
    QJsonArray accept;
    if (obj.value("code").toInt() == 0) {
        accept = obj.value("data").toObject().value("accept_quality").toArray();
    }
    if (accept.isEmpty()) {
        // 非普通类型 (PGC): pgc playurl
        int64_t aid = Crypto::bv2av(bvId);
        QString url2 = QString("https://api.bilibili.com/pgc/player/web/playurl?fnval=4048&fnver=0"
                               "&fourk=1&otype=json&avid=%1&cid=%2&qn=32").arg(aid).arg(cid);
        QString json2 = httpGet(url2, HttpClient::jsonApiHeaders(QString("https://www.bilibili.com/av%1").arg(aid)));
        accept = QJsonDocument::fromJson(json2.toUtf8()).object()
                     .value("result").toObject().value("accept_quality").toArray();
    }
    if (accept.isEmpty())
        return QList<int>{120, 116, 112, 80, 74, 64, 32, 16};
    QList<int> qnList;
    for (const QJsonValue &v : accept)
        qnList.append(v.toInt());
    return qnList;
}

// ============================================================
//  下载链接查询
// ============================================================
QString AbstractBaseParser::getVideoLink(const QString &bvId, const QString &cid, int qn, int downFormat) {
    if (bvId.startsWith("au") && (qn > 3 || qn < 0))
        qn = 3;

    switch (qn) {
        case 800: return getVideoSubtitleLink(bvId, cid);
        case 801:
            m_realQN = qn;
            return QString("https://api.bilibili.com/x/v1/dm/list.so?oid=%1").arg(cid);
        case 3:
        case 2:
        case 1:
        case 0:
            return getAudioLink(bvId, qn);
        default:
            return getVideoLinkByFormat(bvId, cid, qn, downFormat);
    }
}

QString AbstractBaseParser::getAudioLink(const QString &auId, int qn) {
    QString auIdNum = auId.mid(2);
    QString url = QString("https://www.bilibili.com/audio/music-service-c/url?songid=%1"
                          "&privilege=2&quality=%2&mid=&platform=web").arg(auIdNum).arg(qn);
    Logger::info("查询音频链接: " + url);
    QString r = httpGet(url, HttpClient::defaultHeaders());
    QJsonObject data = QJsonDocument::fromJson(r.toUtf8()).object().value("data").toObject();
    int realQn = data.value("type").toInt();
    m_realQN = realQn;
    QJsonArray cdns = data.value("cdns").toArray();
    if (cdns.isEmpty())
        throw BiliException("音频链接查询失败");
    return cdns.first().toString();
}

QString AbstractBaseParser::getVideoSubtitleLink(const QString &bvId, const QString &cid) {
    QString url = QString("https://api.bilibili.com/x/player/wbi/v2?bvid=%1&cid=%2&isGaiaAvoided=false")
                      .arg(bvId, cid);
    url = wbiUrl(url);
    QString result = httpGet(url, HttpClient::jsonApiHeaders());
    m_realQN = 800;
    QJsonObject obj = QJsonDocument::fromJson(result.toUtf8()).object();
    QJsonArray subList = obj.value("data").toObject().value("subtitle").toObject().value("subtitles").toArray();
    if (!subList.isEmpty()) {
        return QString("https:") + subList.first().toObject().value("subtitle_url").toString();
    }
    throw BiliException("未能找到字幕 " + bvId);
}

QString AbstractBaseParser::getVideoLinkByFormat(const QString &bvId, const QString &cid,
                                                 int qn, int downloadFormat) {
    // fnval: MP4(DASH) -> 4048; FLV -> 2
    QString fnval = (downloadFormat & 0x01) == 0 ? "4048" : "2";

    // 先判断视频类型 (普通/PGC)
    QString url = QString("https://api.bilibili.com/x/web-interface/wbi/view/detail?platform=web&bvid=%1")
                      .arg(bvId);
    url = wbiUrl(url);
    QString callBack = httpGet(url, HttpClient::jsonApiHeaders());
    QJsonObject detailRaw = QJsonDocument::fromJson(callBack.toUtf8()).object();

    int64_t aid = 0;
    bool isNormalType = true;
    if (detailRaw.value("code").toInt() == -403) {
        aid = Crypto::bv2av(bvId);
        QString detailUrl = QString("https://api.bilibili.com/x/v3/fav/resource/infos?resources=%1:2"
                                    "&platform=web&folder_mid=&folder_id=").arg(aid);
        QString detailJson = httpGet(detailUrl, HttpClient::jsonApiHeaders());
        QJsonObject detailObj = QJsonDocument::fromJson(detailJson.toUtf8()).object()
                                    .value("data").toArray().at(0).toObject();
        isNormalType = detailObj.value("attr").toInt() != 2;
    } else {
        QJsonObject infoObj = detailRaw.value("data").toObject().value("View").toObject();
        aid = infoObj.value("aid").toVariant().toLongLong();
        isNormalType = infoObj.value("redirect_url").toString().isEmpty();
    }

    QJsonObject dataObj;
    if (isNormalType) {
        QString trylookTail = Config::instance().cookies().isEmpty() ? "&try_look=1" : "";
        QString playUrl;
        if (downloadFormat == 2) {
            playUrl = QString("https://api.bilibili.com/x/player/wbi/playurl?cid=%1&bvid=%2&qn=%3")
                          .arg(cid, bvId).arg(qn);
        } else {
            playUrl = QString("https://api.bilibili.com/x/player/wbi/playurl?cid=%1&bvid=%2&qn=%3"
                              "&type=&otype=json&fnver=0&fnval=%4&fourk=1&gaia_source=&from_client=BROWSER"
                              "&is_main_page=true&need_fragment=false&isGaiaAvoided=false&client_attr=0"
                              "&session=&voice_balance=1&web_location=1315873")
                          .arg(cid, bvId).arg(qn).arg(fnval);
        }
        playUrl += trylookTail;
        playUrl = wbiUrl(playUrl);
        Logger::info("查询播放链接: " + playUrl);
        QString json = httpGet(playUrl, HttpClient::jsonApiHeaders());
        QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
        if (obj.value("code").toInt() != 0)
            throw BiliException(QString("playurl 查询失败(code=%1): %2")
                                    .arg(obj.value("code").toInt()).arg(obj.value("message").toString()));
        dataObj = obj.value("data").toObject();
    } else {
        QString playUrl = QString("https://api.bilibili.com/pgc/player/web/playurl?fnver=0&fourk=1"
                                  "&otype=json&avid=%1&cid=%2&qn=%3&fnval=%4")
                              .arg(aid).arg(cid).arg(qn).arg(fnval);
        Logger::info("查询PGC播放链接: " + playUrl);
        QString json = httpGet(playUrl, HttpClient::jsonApiHeaders(QString("https://www.bilibili.com/av%1").arg(aid)));
        QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
        if (obj.value("code").toInt() != 0)
            throw BiliException(QString("PGC playurl 查询失败(code=%1): %2")
                                    .arg(obj.value("code").toInt()).arg(obj.value("message").toString()));
        dataObj = obj.value("result").toObject();
    }

    int linkQN = dataObj.value("quality").toInt();
    if (qn != linkQN) {
        QJsonObject dash = dataObj.value("dash").toObject();
        if (!dash.isEmpty()) {
            QJsonArray videos = dash.value("video").toArray();
            if (!videos.isEmpty()) {
                int firstLinkQN = videos.first().toObject().value("id").toInt();
                if (linkQN < firstLinkQN)
                    linkQN = firstLinkQN > qn ? qn : firstLinkQN;
            }
        }
    }
    m_realQN = linkQN;
    Logger::info(QString("%1:%2 期望qn=%3 实际qn=%4").arg(bvId, cid).arg(qn).arg(linkQN));

    if (!dataObj.value("dash").toObject().isEmpty()) {
        // DASH 解析失败时回退 FLV (durl)
        try {
            return parseDash(dataObj, linkQN);
        } catch (const std::exception &e) {
            Logger::warn(QString("DASH解析失败, 切换FLV: %1").arg(e.what()));
            return parseFlv(dataObj);
        }
    }
    return parseFlv(dataObj);
}

// ============================================================
//  DASH / FLV 解析
// ============================================================
QString AbstractBaseParser::urlOfMedia(const QJsonObject &media, const api::Headers &checkHeader) {
    QString baseUrl = media.value("base_url").toString();
    if (!Config::instance().checkDashUrl())
        return baseUrl;
    if (HttpClient::instance().checkValid(baseUrl, checkHeader))
        return baseUrl;
    QJsonArray backups = media.value("backup_url").toArray();
    for (const QJsonValue &v : backups) {
        QString u = v.toString();
        if (HttpClient::instance().checkValid(u, checkHeader))
            return u;
    }
    return baseUrl;
}

QString AbstractBaseParser::parseDash(const QJsonObject &data, int &linkQN) {
    QJsonObject dash = data.value("dash").toObject();
    if (dash.isEmpty())
        throw BiliException("无 dash 数据");

    QString link;
    // ---- 视频流 ----
    QJsonArray videos = dash.value("video").toArray();
    QList<QJsonObject> qnVideos;
    for (const QJsonValue &v : videos) {
        QJsonObject video = v.toObject();
        if (video.value("id").toInt() == linkQN)
            qnVideos.append(video);
    }
    if (qnVideos.isEmpty()) {
        if (videos.isEmpty())
            throw BiliException("无视频流");
        QJsonObject v = videos.first().toObject();
        m_realQN = v.value("id").toInt();
        linkQN = m_realQN;
        Logger::warn(QString("API返回质量为%1, 实际只有%2").arg(linkQN).arg(m_realQN));
        qnVideos.append(v);
    }

    // 编码优先级: avc(7) > hevc(12) > av1(13)
    // codecid: 7=AVC, 12=HEVC, 13=AV1
    int codecPriority = Config::instance().videoCodecPriority();
    QJsonObject chosen;
    for (const QJsonObject &v : qnVideos) {
        int codecid = v.value("codecid").toInt();
        if (codecid == codecPriority) {
            chosen = v;
            break;
        }
    }
    if (chosen.isEmpty())
        chosen = qnVideos.first();

    api::Headers checkHeader = HttpClient::m4sHeaders();
    link = urlOfMedia(chosen, checkHeader);
    link += "#";

    // ---- 音频流 ----
    QList<QJsonObject> audios;
    QJsonArray audioArr = dash.value("audio").toArray();
    for (const QJsonValue &v : audioArr)
        audios.append(v.toObject());
    QJsonObject dolby = dash.value("dolby").toObject();
    if (!dolby.isEmpty() && linkQN == 126) {
        QJsonArray dAudio = dolby.value("audio").toArray();
        for (const QJsonValue &v : dAudio)
            audios.append(v.toObject());
    }
    QJsonObject flac = dash.value("flac").toObject();
    if (!flac.isEmpty()) {
        QJsonObject flacAudio = flac.value("audio").toObject();
        if (!flacAudio.isEmpty())
            audios.append(flacAudio);
    }
    if (!audios.isEmpty()) {
        // 音质优先级: 30280(192K) > 30232 > 30216 ...
        int audioPri = Config::instance().audioQualityPriority();
        QJsonObject chosenAudio;
        for (const QJsonObject &a : audios) {
            if (a.value("id").toInt() == audioPri) {
                chosenAudio = a;
                break;
            }
        }
        if (chosenAudio.isEmpty())
            chosenAudio = audios.first();
        link += urlOfMedia(chosenAudio, checkHeader);
    }
    return link;
}

QString AbstractBaseParser::parseFlv(const QJsonObject &data) {
    QJsonArray urlList = data.value("durl").toArray();
    if (urlList.isEmpty())
        throw BiliException("无 durl 数据");
    if (urlList.size() == 1)
        return urlList.first().toObject().value("url").toString();

    // 多段: order + url 拼接
    QStringList parts;
    for (const QJsonValue &v : urlList) {
        QJsonObject obj = v.toObject();
        parts << QString("%1%2").arg(obj.value("order").toInt()).arg(obj.value("url").toString());
    }
    return parts.join("#");
}

} // namespace bili::parser
