#ifndef BILI_CODE_MODEL_TYPES_H
#define BILI_CODE_MODEL_TYPES_H

#include <QString>
#include <QHash>
#include <QVariant>
#include <QMetaType>
#include <cstdint>

namespace bili::model {

/**
 * @brief 视频分P/片段信息 (对应 Java 版 ClipInfo)
 *        avId + cId 唯一确定一个可下载片段
 */
struct ClipInfo {
    QString avTitle;      // 合集/视频标题
    QString avId;         // BV号 / av号 / au号 / ss号
    int64_t cid = 0;      // B站 cid
    int page = 1;         // 分P序号
    QString title;        // 分P标题
    QString picPreview;   // 封面
    QString listName;     // 集合名称 (收藏夹/合集)
    QString listOwnerName;
    QString listOwnerId;
    QString upName;       // UP主
    QString upId;
    int64_t favTime = 0;  // 收藏时间 (ms)
    int64_t cTime = 0;    // 发布时间 (ms)
    int remark = -1;      // 集合内显示序号

    // 清晰度 -> 下载链接 (查询时填充)
    QHash<int, QString> links;

    int displayOrder() const { return remark < 0 ? page : remark; }

    bool operator==(const ClipInfo &o) const { return cid == o.cid; }
};

/**
 * @brief 视频/合集信息 (对应 Java 版 VideoInfo)
 */
struct VideoInfo {
    QString videoName;
    QString videoId;      // BV/ss/ml/au...
    QString author;
    QString authorId;
    QString videoPreview;
    QString brief;
    QString videoLink;    // 原始输入链接

    // cid -> ClipInfo (保持插入顺序)
    QList<ClipInfo> clips;

    // 集合类型标记 (用于合集合并功能判断)
    bool isCollection = false;  // 是否为多视频集合 (合集/番剧/收藏夹)

    int clipCount() const { return static_cast<int>(clips.size()); }
};

/**
 * @brief 下载任务状态
 */
enum class TaskStatus {
    Queued,       // 队列中
    Parsing,      // 解析中
    Downloading,  // 下载中
    Converting,   // 转码/合并中
    Merging,      // 合集合并中
    Success,      // 成功
    Failed,       // 失败
    Stopped       // 已停止
};

inline QString taskStatusText(TaskStatus s) {
    switch (s) {
        case TaskStatus::Queued:      return QStringLiteral("队列中");
        case TaskStatus::Parsing:     return QStringLiteral("解析中");
        case TaskStatus::Downloading: return QStringLiteral("下载中");
        case TaskStatus::Converting:  return QStringLiteral("转码中");
        case TaskStatus::Merging:     return QStringLiteral("合并中");
        case TaskStatus::Success:     return QStringLiteral("完成");
        case TaskStatus::Failed:      return QStringLiteral("失败");
        case TaskStatus::Stopped:     return QStringLiteral("已停止");
    }
    return QString();
}

/**
 * @brief 清晰度枚举 (qn 值与B站一致)
 */
enum VideoQuality {
    QN_240P = 6,
    QN_360P = 16,
    QN_480P = 32,
    QN_720P = 64,
    QN_720P60 = 74,
    QN_1080P = 80,
    QN_1080PPlus = 112,
    QN_1080P60 = 116,
    QN_4K = 120,
    QN_HDR = 125,
    QN_Dolby = 126,
    QN_8K = 127,
};

inline QString videoQualityText(int qn) {
    switch (qn) {
        case 127: return QStringLiteral("8K 超高清");
        case 126: return QStringLiteral("杜比视界");
        case 125: return QStringLiteral("HDR 真彩");
        case 120: return QStringLiteral("4K 超清");
        case 116: return QStringLiteral("1080P60");
        case 112: return QStringLiteral("1080P+ 高码率");
        case 80:  return QStringLiteral("1080P 高清");
        case 74:  return QStringLiteral("720P60");
        case 64:  return QStringLiteral("720P 高清");
        case 32:  return QStringLiteral("480P 清晰");
        case 16:  return QStringLiteral("360P 流畅");
        case 6:   return QStringLiteral("240P 流畅");
        default:   return QStringLiteral("未知清晰度");
    }
}

/**
 * @brief 下载格式
 */
enum DownloadFormat {
    Format_MP4_DASH = 0,  // DASH 音视频分流 (m4s) -> mp4
    Format_FLV = 1,       // FLV
    Format_AudioOnly = 2, // 仅音频
};

/**
 * @brief 单个下载任务 (对应 Java 版 TaskInfo)
 *        一个任务 = 一个 VideoInfo 的下载 (可能包含多个 clip)
 */
struct DownloadTask {
    // ---- 输入 ----
    QString inputUrl;          // 用户输入的原始 url
    VideoInfo videoInfo;       // 解析结果
    int qn = 80;               // 期望清晰度
    int format = Format_MP4_DASH;
    bool mergeCollection = false;      // 合集合并为单个MP4
    bool deleteTempAfterMerge = true;  // 合并后删除中间文件

    // ---- 运行时状态 ----
    int taskId = 0;
    TaskStatus status = TaskStatus::Queued;
    QString statusText;
    QString errorMessage;

    int totalClips = 0;        // 总分P数
    int doneClips = 0;         // 已完成分P数
    int mergedClips = 0;       // 已合并分P数 (合集合并阶段)

    qint64 totalBytes = 0;     // 总大小 (估算/实际)
    qint64 receivedBytes = 0;  // 已下载
    double speedBytesPerSec = 0; // 瞬时速度

    // 每个分P的独立进度 (索引与 videoInfo.clips 对应)
    struct ClipProgress {
        qint64 received = 0;
        qint64 total = 0;
        double speed = 0;
        TaskStatus status = TaskStatus::Queued;
        QString outputFile;
        QString sha256;      // 下载完成后计算的 SHA256 (用于合并前校验)
    };
    QList<ClipProgress> clipProgress;

    QString currentClipTitle;  // 当前处理的分P标题
    QString outputFile;        // 最终输出文件 (单文件模式)
    QStringList outputFiles;   // 输出文件列表 (多文件模式)

    // ---- 派生 ----
    double progress() const {
        if (totalBytes > 0)
            return static_cast<double>(receivedBytes) / static_cast<double>(totalBytes);
        if (totalClips > 0)
            return static_cast<double>(doneClips) / static_cast<double>(totalClips);
        return 0.0;
    }
    QString displayName() const {
        return videoInfo.videoName.isEmpty() ? inputUrl : videoInfo.videoName;
    }
};

} // namespace bili::model

Q_DECLARE_METATYPE(bili::model::ClipInfo)
Q_DECLARE_METATYPE(bili::model::VideoInfo)
Q_DECLARE_METATYPE(bili::model::DownloadTask)

#endif // BILI_CODE_MODEL_TYPES_H
