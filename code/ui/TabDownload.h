#ifndef BILI_CODE_UI_TAB_DOWNLOAD_H
#define BILI_CODE_UI_TAB_DOWNLOAD_H

#include "model/Types.h"
#include "downloader/DownloadEngine.h"

#include <QWidget>
#include <QTreeWidget>
#include <QTimer>

class QPushButton;

namespace bili::ui {

/**
 * @brief 下载列表标签页 (树形任务池)
 *        顶层 = 任务(视频); 展开后子项 = 每个分P, 显示独立进度/SHA256状态
 *        单项视频即单层任务; 多分P视频集可展开查看每个分P进度
 */
class TabDownload : public QWidget {
    Q_OBJECT
public:
    explicit TabDownload(QWidget *parent = nullptr);

private slots:
    void onTaskAdded(int taskId);
    void onTaskRemoved(int taskId);
    void onTaskProgress(int taskId, qint64 received, qint64 total, double speed);
    void onClipProgress(int taskId, int clipIndex, qint64 received, qint64 total, double speed);
    void onClipDone(int taskId, int clipIndex, const QString &file, int doneCount, int totalCount,
                    const QString &sha256);
    void onTaskStatusChanged(int taskId, model::TaskStatus status, const QString &text);
    void onTaskFinished(int taskId, bool success, const QString &error);
    void onStartClicked();
    void onStopClicked();
    void onRemoveClicked();
    void onStartAllClicked();
    void onStopAllClicked();
    void onOpenFolderClicked();
    void refreshSpeeds();

private:
    void setupUi();
    QTreeWidgetItem *topItemOf(int taskId) const;
    void updateTopProgress(int taskId);

    static QString formatSize(qint64 bytes);
    static QString formatSpeed(double bytesPerSec);

    QTreeWidget *m_tree = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_startAllBtn = nullptr;
    QPushButton *m_stopAllBtn = nullptr;
    QTimer *m_refreshTimer = nullptr;
};

} // namespace bili::ui

#endif // BILI_CODE_UI_TAB_DOWNLOAD_H
