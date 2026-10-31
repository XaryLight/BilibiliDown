#ifndef BILI_CODE_UI_TAB_PARSE_H
#define BILI_CODE_UI_TAB_PARSE_H

#include "model/Types.h"

#include <QWidget>
#include <QFutureWatcher>

class QLineEdit;
class QPushButton;
class QComboBox;
class QTableWidget;
class QLabel;
class QCheckBox;
class QSpinBox;
class QGroupBox;

namespace bili::ui {

/**
 * @brief 解析下载标签页
 *   输入 URL -> 解析出分P列表 -> 选择清晰度/格式 -> 加入下载队列
 *   支持 "合集合并为一个MP4" 选项
 */
class TabParse : public QWidget {
    Q_OBJECT
public:
    explicit TabParse(QWidget *parent = nullptr);
    ~TabParse() override;

signals:
    void loginStateChanged(bool logged, const QString &userName);

private slots:
    void onParseClicked();
    void onParseFinished();
    void onDownloadClicked();
    void onInputChanged();
    void onQualityChanged(int index);

private:
    void setupUi();
    void updateClipTable();
    void updateDownloadButton();
    QString defaultFormat() const;
    int selectedQn() const;

    // ---- 输入区 ----
    QLineEdit *m_inputEdit = nullptr;
    QPushButton *m_parseBtn = nullptr;
    QLabel *m_parseStatus = nullptr;

    // ---- 视频信息区 ----
    QLabel *m_videoTitle = nullptr;
    QLabel *m_videoAuthor = nullptr;
    QLabel *m_videoBrief = nullptr;
    QLabel *m_videoPreview = nullptr;
    QTableWidget *m_clipTable = nullptr;

    // ---- 下载选项 ----
    QComboBox *m_qualityCombo = nullptr;
    QComboBox *m_formatCombo = nullptr;
    QCheckBox *m_mergeCheck = nullptr;     // 合集合并为单个MP4
    QCheckBox *m_deleteTempCheck = nullptr; // 合并后删除中间文件
    QPushButton *m_downloadBtn = nullptr;

    // ---- 解析结果 ----
    model::VideoInfo m_videoInfo;
    bool m_parsed = false;
    bool m_parsing = false;

    // ---- 异步解析 ----
    QFutureWatcher<model::VideoInfo> *m_watcher = nullptr;
};

} // namespace bili::ui

#endif // BILI_CODE_UI_TAB_PARSE_H
