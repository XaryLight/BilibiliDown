#ifndef BILI_API_FFMPEG_H
#define BILI_API_FFMPEG_H

#include <QString>
#include <QStringList>

namespace bili::api {

/**
 * @brief ffmpeg 外部进程封装
 *         - 音视频合并 (m4s + m4s -> mp4)
 *         - 片段拼接 (flv/mp4 concat -> 单个文件)
 *         - 合集合并 (多个 mp4 -> 单个 mp4)  [流拷贝优先, 失败回退重编码]
 */
class Ffmpeg {
public:
    static Ffmpeg &instance();

    // 设置 ffmpeg 可执行文件路径 (默认 "ffmpeg", 需在 PATH 或同目录)
    void setExecutable(const QString &path);
    QString executable() const;

    // 自动解析 ffmpeg 路径: 依次尝试 程序目录/bin/ffmpeg.exe, 程序目录/ffmpeg.exe, PATH
    // 找到返回 true
    bool autoDetect();

    // 探测 ffmpeg 是否可用
    bool available(QString *versionOut = nullptr) const;

    // 1) 音视频合并: -i video -i audio -c copy dst  (任一路可为 null)
    bool mergeVideoAudio(const QString &videoPath, const QString &audioPath,
                         const QString &dstPath, QString *errorOut = nullptr) const;

    // 2) 单流转封装: -i src -c copy dst (用于 m4s->mp4 容器转换)
    bool remux(const QString &srcPath, const QString &dstPath, QString *errorOut = nullptr) const;

    // 3) concat 拼接: 输入为文件路径列表 (同编码参数), -f concat -safe 0 -i list.txt -c copy dst
    //    优先流拷贝; 若失败且 allowReencode=true, 使用 filter_complex 重编码后拼接
    bool concat(const QStringList &srcFiles, const QString &dstPath,
                bool allowReencode = true,
                const std::function<void(int, int)> &progress = nullptr,
                QString *errorOut = nullptr) const;

    // 4) 合集合并: 将多个中间 mp4 合并为单个 mp4 (封装 concat, 带日志)
    bool mergeClipsToSingleMp4(const QStringList &clipMp4s, const QString &dstPath,
                               const std::function<void(int, int)> &progress = nullptr,
                               QString *errorOut = nullptr) const;

private:
    Ffmpeg() = default;
    QString m_executable = "ffmpeg";

    // 运行一次进程, 返回退出码; 输出捕获到 log
    int run(const QStringList &args, const QString &workingDir, QString *stderrOut = nullptr) const;
};

} // namespace bili::api

#endif // BILI_API_FFMPEG_H
