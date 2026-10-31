#ifndef BILI_CODE_UI_TAB_DOWNLOAD_H
#define BILI_CODE_UI_TAB_DOWNLOAD_H

#include "model/Types.h"
#include "downloader/DownloadEngine.h"

#include <QWidget>
#include <QTableView>
#include <QStandardItemModel>
#include <QTimer>

class QPushButton;

namespace bili::ui {

/**
 * @brief 下载列表标签页: 任务表格 + 进度 + 控制按钮
 */
class TabDownload : public QWidget {
    Q_OBJECT
public:
    explicit TabDownload(QWidget *parent = nullptr);

private slots:
    void onTaskAdded(int taskId);
    void onTaskRemoved(int taskId);
    void onTaskProgress(int taskId, qint64 received, qint64 total, double speed);
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
    int rowOfTask(int taskId) const;
    QString formatSize(qint64 bytes) const;
    QString formatSpeed(double bytesPerSec) const;

    QTableView *m_table = nullptr;
    QStandardItemModel *m_model = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_startAllBtn = nullptr;
    QPushButton *m_stopAllBtn = nullptr;
    QTimer *m_refreshTimer = nullptr;
};

} // namespace bili::ui

#endif // BILI_CODE_UI_TAB_DOWNLOAD_H
